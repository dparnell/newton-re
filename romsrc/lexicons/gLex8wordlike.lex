# gLex8wordlike: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "abcdefghijklmnoprstuvwxyzáàâäãåçéèêëíìîïñóòôöõúùûüßæœÿ" "t0"
set s1 "u" "t1"
set s2 "q" "t2"
set s3 "abcdefghijklmnopqrstuvwxyzáàâäãåçéèêëíìîïñóòôöõúùûüßæœÿ" "t3"
set s4 "/" "t4"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s0 word -> n5
node n1 s2 last -> n9
node n2 s0 word -> n2
node n3 s2 last -> n4
node n4 s1 word last -> n2
node n5 s0 word -> n2
node n6 s2 -> n4
node n7 s4 last -> n8
node n8 s3 word last
node n9 s1 word -> n2
node n10 s4 last -> n8
