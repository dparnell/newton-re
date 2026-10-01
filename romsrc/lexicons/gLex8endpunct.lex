# gLex8endpunct: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "." "t0"
set s1 "," "t1"
set s2 "s" "t2"
set s3 "'" "t3"
set s4 "!,:;?s" "t4"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s0 word -> n3
node n1 s3 -> n6
node n2 s4 word last
node n3 s0 -> n5
node n4 s1 word last
node n5 s0 word last
node n6 s2 word last
