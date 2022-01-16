## HP-GL plot file re-sequencer
(c) fenugrec 2021-2022

Simple tool to change the order in which HPGL plots are drawn.

Original problem (see typical example in `demo/`) is where some devices like the HP 4195A draw elements in a silly order which causes graticule lines to appear on top of the important stuff, i.e. traces.

So for the 4195A example, the plot is normally drawn in this sequence :

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

## usage
By default this tool will reorder pens in the hardcoded order "3,4,5,6,7,1,2" , but this can be changed with an optional argument of the form `-r PPP...`) where each P is a digit 1-9 to specify the pen ordering.
For example, `-r 3456712` is equivalent to default behaviour. Any pen number not listed will not be drawn.


## example usage:
`./reorder orig.plt better.plt `


Licensed under GPLv3.
