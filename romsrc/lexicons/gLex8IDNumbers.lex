# gLex8IDNumbers: a ROM lexicon (an Airus AL dictionary): a graph whose nodes each
# stand for a set of characters.  Built by tools/lexicons/newtonlex.py;
# the format is in that file and in tools/lexicons/README.md.
type 0x01

# the character sets, each with the tag the recogniser is told when a
# character of it matched
set s0 ")" "t0"
set s1 "0123456789" "t1"
set s2 "(" "t2"
set s3 "ABCDEFGHIJKLMNOPRSTUVWXY" "t3"
set s4 "ABCDEFGHIJKLMNPRSTUVWXY" "t4"
set s5 "O" "t5"

# the nodes, in the order they lie (the first row is the root's): the
# set, 'word' if a word may end here, 'last' on the last of a row, the
# first child after '->'
node n0 s2 -> n49
node n1 s1 -> n41
node n2 s5 -> n52
node n3 s4 word last -> n44
node n4 s1 -> n4
node n5 s0 last -> n41
node n6 s1 -> n4
node n7 s4 -> n38
node n8 s5 last -> n40
node n9 s0 word -> n27
node n10 s1 last -> n9
node n11 s1 -> n9
node n12 s4 -> n24
node n13 s5 last -> n26
node n14 s0 word -> n17
node n15 s1 last -> n14
node n16 s1 last -> n14
node n17 s2 -> n16
node n18 s1 word last -> n17
node n19 s0 word -> n17
node n20 s1 last -> n19
node n21 s1 last -> n19
node n22 s2 -> n21
node n23 s1 word last -> n17
node n24 s0 word -> n22
node n25 s3 last -> n24
node n26 s3 last -> n24
node n27 s2 -> n11
node n28 s1 word -> n27
node n29 s4 word last -> n30
node n30 s3 word -> n30
node n31 s2 -> n21
node n32 s1 word last -> n17
node n33 s0 word -> n27
node n34 s1 last -> n33
node n35 s1 last -> n33
node n36 s2 -> n35
node n37 s1 word last -> n27
node n38 s0 word -> n36
node n39 s3 last -> n38
node n40 s3 last -> n38
node n41 s2 -> n6
node n42 s1 -> n41
node n43 s4 word last -> n44
node n44 s3 word -> n44
node n45 s2 -> n35
node n46 s1 word last -> n27
node n47 s0 -> n41
node n48 s1 last -> n47
node n49 s1 -> n47
node n50 s4 -> n38
node n51 s5 last -> n40
node n52 s4 word -> n44
node n53 s2 last -> n54
node n54 s4 -> n38
node n55 s5 last -> n40
