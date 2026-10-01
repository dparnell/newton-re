; NewtonScript highlighting, shared by Zed and the tree-sitter CLI
; (editor-plugins/tree-sitter-newtonscript/tree-sitter.json points here).
; General patterns first: when several match one node, the later wins.

(identifier) @variable

(parameter
  name: (identifier) @variable.parameter)

(type) @type
(class_name) @type

(slot_name) @property

(message_name) @function.method

(call_expression
  function: (identifier) @function)

(function_declaration
  name: (identifier) @function)

(global_declaration
  name: (identifier) @function
  parameters: (parameter_list))

(assignment
  left: (identifier) @function
  right: (function))

(slot
  name: (slot_name) @function.method
  value: (function))

(self) @variable.special
(inherited) @variable.special

; foreach ... in x collect y: the verb is any symbol
(foreach_expression
  verb: (identifier) @keyword)

[
  "begin"
  "end"
  "if"
  "then"
  "else"
  "loop"
  "for"
  "to"
  "by"
  "do"
  "foreach"
  "deeply"
  "in"
  "while"
  "repeat"
  "until"
  "try"
  "onexception"
  "local"
  "constant"
  "global"
  "func"
  "native"
  "break"
  "return"
  "call"
  "with"
  "exists"
] @keyword

[
  "and"
  "or"
  "not"
  "div"
  "mod"
] @keyword.operator

[
  ":="
  "+"
  "-"
  "*"
  "/"
  "&"
  "&&"
  "<"
  ">"
  "<="
  ">="
  "="
  "<>"
  "<<"
  ">>"
] @operator

[
  "."
  ","
  ";"
  ":"
  ":?"
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

(integer) @number
(real) @number

(nil) @constant.builtin
(true) @boolean
(magic_pointer) @constant.builtin
(immediate) @constant

(string) @string
(escape_sequence) @string.escape
(character) @string.special

(quoted) @string.special.symbol
(exception_name) @string.special.symbol

(comment) @comment
(line_directive) @preproc
(record_marker) @preproc
