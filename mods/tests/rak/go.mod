module rakharness

go 1.22

require github.com/sandertv/go-raknet v0.0.0

// Point this at a checkout of github.com/sandertv/go-raknet (see tests/rak/README.md).
replace github.com/sandertv/go-raknet => ../../../df-mc/go-raknet
