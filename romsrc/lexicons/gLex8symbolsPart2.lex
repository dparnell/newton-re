# gLex8symbolsPart2: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 "ÄÅÇÉÑÖÜáàâäãåçéèêëíìîïñóòôöõúùûü°¢£§¶ßÆØ¥ªºæø¿¡«»ÀÃÕŒœÿŸ„ÂÊÁËÈÍÎÏÌÓÔÒÚÛÙ" "t0"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s0 word last -> n0
