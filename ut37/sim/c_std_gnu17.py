# GCC 15 defaults C to C23, where `bool` is a keyword; ricmoo/QRCode's
# `typedef uint8_t bool;` then fails. Pin C sources (not C++) to gnu17.
Import("env")

for e in (env, DefaultEnvironment()):
    e.Append(CFLAGS=["-std=gnu17"])
