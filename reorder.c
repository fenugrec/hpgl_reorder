/*
 * (c) fenugrec 2021-2022
 * GPLv3
 *
 * typical HP 4195A PLT :
 * SP5 for green comments/text
 * SP3 grey text
 * SP1 yellow text ?
 * SP2 cyan text ?
 * SP4 white text
 * SP1 yellow trace
 * SP2 cyan trace
 * SP3 grey graticule
 *
 * This will default to drawing 3,4,5,6,7,1,2, in that order.
 * Optional arg in hp2xx style : '-r PPPP...' where each P
 * is a digit 1-7. Last digits drawn last, on top.
 */


#include <errno.h>
#include <ctype.h>	//for isdigit()
#include <limits.h> //UINT_MAX
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "stypes.h"

#define file_maxsize (UINT_MAX)	//arbitrary limit


#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

#define MAX_PEN	7	//SP0 to SP7 are valid pen
#define MAX_CHUNKS 20	//any more than this is suspicious
#define OPCODE_LEN 3	//"SPn"

const unsigned default_order[] = {3,4,5,6,7,1,2,0};	//good for HP 4195A

// hax, get file length but restore position
u32 flen(FILE *hf) {
	long siz;
	long orig;

	if (!hf) return 0;
	orig = ftell(hf);
	if (orig < 0) return 0;

	if (fseek(hf, 0, SEEK_END)) return 0;

	siz = ftell(hf);
	if (siz < 0) siz=0;
		//the rest of the code just won't work if siz = UINT32_MAX
	#if (LONG_MAX >= UINT32_MAX)
		if ((long long) siz == (long long) UINT32_MAX) siz = 0;
	#endif

	if (fseek(hf, orig, SEEK_SET)) return 0;
	return (u32) siz;
}



struct pen_chunk {
	unsigned pen;	//1-7. Pen 0 marks end of file
	unsigned start;	//offset in src file of "SPx.....;" chunk
	unsigned len;	//# of bytes
};

/** identify all chunks and return a new array of pen_chunk structs
 *
 * @return NULL if error. Caller must free array
 */
struct pen_chunk *find_chunks(const u8 *src, unsigned len) {
	// Find "SP" opcodes and grow array every occurence.
	// we'll still emit a chunk with pen_0 to mark end of array
	unsigned num_chunks = 0;	//# of chunks

	struct pen_chunk *pchunks = calloc(MAX_CHUNKS, sizeof(struct pen_chunk));
	if (!pchunks) {
		printf("malloc choke\n");
		return NULL;
	}

	unsigned idx;
	for (idx=0; idx <= (len - OPCODE_LEN); idx++) {
		if (memcmp(&src[idx], "SP", 2) != 0) {
			continue;
		}
		//found one. src[idx] is the leading 'S'
		//so src[idx + 2] == pen_no
		if (!isdigit(src[idx+2])) {
			printf("bad SP opcode ! in '%.*s'\n", OPCODE_LEN, (const char *) &src[idx]);
			goto badchunks;
		}

		unsigned pen;
		pen = src[idx+2] - '0';
		pchunks[num_chunks].pen = pen;
		pchunks[num_chunks].start = idx;
		if (num_chunks > 0) {
			//compute len of previous chunk
			unsigned prevstart = pchunks[num_chunks - 1].start;
			pchunks[num_chunks - 1].len = idx - prevstart;
		}
		printf("\tChunk %u @ %u: pen %u\n", num_chunks, idx, pen);
		num_chunks++;
		if (num_chunks == MAX_CHUNKS) {
			printf("that's a lot of chunks (%u), still have some file left to parse, %u\n",
					num_chunks, len - idx);
			break;
		}
	}
	if (num_chunks == 0) {
		printf("no chunks ??\n");
		goto badchunks;
	}
	//compute length for last chunk
	pchunks[num_chunks - 1].len = len - pchunks[num_chunks - 1].start;

	return pchunks;

badchunks:
	free(pchunks);
	return NULL;
}

/** Parse source and write all chunks of specified pen.
 *
 * @param pchunks : describes all chunks in src buffer
 * @param pen : 0 to MAX_PEN
 *
 * @return 1 if ok
 */
static bool lift_chunk(FILE *outf, const u8 *src, const struct pen_chunk *pchunks, unsigned pen) {
	unsigned idx;
	for (idx=0; idx < MAX_CHUNKS; idx++) {
		if (pchunks[idx].len == 0) {
			//done
			break;
		}
		unsigned start, clen, curpen;
		curpen = pchunks[idx].pen;
		if (pen != curpen) {
			//skip these for now
			continue;
		}
		start = pchunks[idx].start;
		clen = pchunks[idx].len;
		if (fwrite(&src[start], 1, clen, outf) !=  clen) {
			printf("SPx: fwrite err\n");
			return 0;
		}
		printf("wrote %u bytes of SP%u\n", clen, pen);
	}
	return 1;
}

/** reorder chunks from ifile into outf, according to specified order
 *
 * @param neworder array of up to MAX_PEN+1 numbers. Last elements are drawn last, on top. Last pen must be 0 !
 *
 */
static void reorder(FILE *ifile, FILE *outf, const unsigned neworder[MAX_PEN+1] ) {
	u32 file_len;

	// Check for duplicates and 0 termination in new order
	bool checklist[MAX_PEN+1] = {0};
	unsigned idx;
	for (idx=0; idx <= MAX_PEN; idx++) {
		unsigned curpen = neworder[idx];
		if (curpen > MAX_PEN) {
			printf("Invalid pen %u specified ?\n", curpen);
			return;
		}
		if (checklist[curpen]) {
			printf("Duplicate pen %u ?\n", curpen);
			return;
		}
		checklist[curpen] = 1;
		if (curpen ==0) {
			//done
			break;
		}
	}
	if (!checklist[0]) {
		printf("Unterminated pen ordering ?\n");
		return;
	}

	rewind(ifile);
	file_len = flen(ifile);
	if ((!file_len) || (file_len > file_maxsize)) {
		printf("bad file length %lu\n", (unsigned long) file_len);
		return;
	}

	u8 *src = malloc(file_len);
	if (!src) {
		printf("malloc choke\n");
		goto freebufs;
	}

	/* load whole file */
	if (fread(src,1,file_len,ifile) != file_len) {
		printf("trouble reading\n");
		goto freebufs;
	}

	struct pen_chunk *pchunks = find_chunks(src, file_len);
	if (!pchunks) {
		printf("no chunks.\n");
		goto freebufs;
	}

	// first, write header
	if (pchunks[0].len == 0) {
		printf("bad chunk 0\n");
		goto freebufs;
	}
	unsigned hdr_len = pchunks[0].start;
	if (fwrite(src, 1, hdr_len, outf) !=  hdr_len) {
		printf("hdr: fwrite err\n");
		goto freebufs;
	}

	//write chunks in order, SP0 last naturally since it serves as list terminator
	for (idx = 0; idx <= MAX_PEN; idx++) {
		bool rv=lift_chunk(outf, src, pchunks, neworder[idx]);
		if (!rv) {
			//subfunction already printed error msg
			goto freebufs;
		}
		if (neworder[idx] == 0) {
			//all done
			break;
		}
	}

freebufs:
	if (pchunks) free(pchunks);
	if (src) free(src);
	return;
}


/********* main stuff (options etc) *******************/

static void usage(void)
{
	fprintf(stderr, "usage:\n"
		"reorder <in_file> <out_file> [-r PPP...]\n"
		"Or specify filenames explicitly with\n"
		"\t-i <filename>\tinput PLT file\n"
		"\t-o <filename>\toutput PLT file\n"
		"Optional argument -r to specify pen ordering; default 3456712.\n"
		"e.g. \"-r 3412\" will output pens 3,4,1,2, with pen 2 last (on top).\n"
		"");
}


int main(int argc, char * argv[]) {
	char c;
	int index;
	FILE *ifile = NULL;
	FILE *ofile = NULL;
	unsigned ordering[MAX_PEN + 1];
	unsigned order_idx = 0;

	printf(	"**** %s\n"
		"**** (c) 2021 fenugrec\n", argv[0]);

	while((c = getopt(argc, argv, "i:o:r:h")) != -1) {
		switch(c) {
		case '?':
			//fallthrough
		case 'h':
			usage();
			goto goodexit;
		case 'i':
			if (ifile) {
				fprintf(stderr, "-f given twice");
				goto bad_exit;
			}
			ifile = fopen(optarg, "rb");
			if (!ifile) {
				fprintf(stderr, "fopen() failed: %s\n", strerror(errno));
				goto bad_exit;
			}
			break;
		case 'o':
			if (ofile) {
				fprintf(stderr, "-o given twice");
				goto bad_exit;
			}
			ofile = fopen(optarg, "wb");
			if (!ofile) {
				fprintf(stderr, "fopen() failed: %s\n", strerror(errno));
				goto bad_exit;
			}
			break;
		case 'r':
			{
			//parse something like "456712" => {4,5,6,7,1,2,0} (append 0 if missing)
			unsigned cur;
			for (cur=0; optarg[cur] != 0; cur++) {
				if (order_idx == MAX_PEN) {
					fprintf(stderr, "too many pens !\n");
					goto bad_exit;
				}
				if (!isdigit(optarg[cur])) {
					fprintf(stderr, "bad pen number, not a digit\n");
					goto bad_exit;
				}
				unsigned digit = optarg[cur] - '0';
				if (digit > MAX_PEN) {
					fprintf(stderr, "bad pen number %u > %u\n", digit, MAX_PEN);
					goto bad_exit;
				}
				if (digit == 0) {
					fprintf(stderr, "do not specify pen 0 (last pen)\n");
					goto bad_exit;
				}
				ordering[order_idx++] = digit;
				//we'll check for dupes etc later
			}
			//ensure 0 pen at the end
			ordering[order_idx] = 0;
			break;
			}
		default:
			usage();
			goto bad_exit;
		}
	}

	if (optind < 1) {
		usage();
		goto bad_exit;
	}

	//second loop for non-option args
	for (index = optind; index < argc; index++) {
		if (!ifile) {
			ifile = fopen(argv[index], "rb");
			if (!ifile) {
				fprintf(stderr, "fopen() failed: %s\n", strerror(errno));
				goto bad_exit;
			}
			continue;
		}
		if (!ofile) {
			ofile = fopen(argv[index], "wb");
			if (!ofile) {
				fprintf(stderr, "fopen() failed: %s\n", strerror(errno));
				goto bad_exit;
			}
			continue;
		}
		fprintf(stderr, "junk argument\n");
		goto bad_exit;
	}

	if (!ifile || !ofile) {
		printf("some missing args.\n");
		usage();
		goto bad_exit;
	}

	const unsigned *neworder = default_order;
	if (order_idx > 0) {
		// an ordering was specified : use that
		neworder = ordering;
	}
	reorder(ifile, ofile, neworder);

goodexit:
	if (ifile) {
		fclose(ifile);
	}
	if (ofile) {
		fclose(ofile);
	}
	return 0;

bad_exit:
	if (ifile) {
		fclose(ifile);
	}
	if (ofile) {
		fclose(ofile);
	}
	return 1;
}

