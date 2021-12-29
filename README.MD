## HP-GL plot file re-sequencer

Crude tool to change the order in which HPGL plots are drawn.
Currently hardcoded to work with HP 4195A outputs which draws the grid on top of signal traces !

See example in `demo/`.

Eventually could take an arg in hp2xx style (e.g. `-r PPPPPP`) where each P is a digit 1-9 to specify the pen ordering.


(c) fenugrec 2021

Licensed under GPLv3


## how it works
A typical HP 4195A plot draws in this sequence :

```
SP5 for green comments/text
SP3 grey text
SP1 yellow text for trace A
SP2 cyan text for trace B
SP4 white text
SP1 yellow trace A
SP2 cyan trace B
SP3 grey graticule
SP0 terminator (empty)
```

so we just need to output almost everything and save SP1, SP2 and then SP0 for last.


## example usage:
`./reorder orig.plt better.plt `

