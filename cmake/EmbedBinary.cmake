# cmake -DINPUT=<file> -DOUTPUT=<cpp> -DSYMBOL=<name> -DNAMESPACE=<ns> -P EmbedBinary.cmake
file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _len)
math(EXPR _bytes "${_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _body "${_hex}")
file(WRITE "${OUTPUT}" "#include <cstddef>\nnamespace ${NAMESPACE} {\n    extern const unsigned char ${SYMBOL}[] = {${_body}};\n    extern const size_t ${SYMBOL}Size = ${_bytes};\n}\n")
