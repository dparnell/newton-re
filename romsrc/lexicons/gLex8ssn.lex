# gLex8ssn: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "0123456789" "t0"
set s1 "-" "t1"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s0 last -> n9
node n1 s0 last -> n10
node n2 s0 last -> n1
node n3 s0 last -> n2
node n4 s1 last -> n3
node n5 s0 last -> n4
node n6 s0 last -> n5
node n7 s1 last -> n6
node n8 s0 last -> n7
node n9 s0 last -> n8
node n10 s0 word last
