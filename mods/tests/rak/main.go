// Test harness for Perlin's RakNet client: a go-raknet server that echoes game packets (0xFE...), answers the
// "BIG" and "COUNT" requests, plus an optional lossy UDP relay in front of it.
//
//	go run . -listen 127.0.0.1:19232 -relay 127.0.0.1:19233 -drop 0.2
package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"math/rand"
	"net"
	"sync"
	"time"

	"github.com/sandertv/go-raknet"
)

func pattern(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i*7 + i/251)
	}
	return b
}

func serve(c *raknet.Conn) {
	defer c.Close()
	for {
		pk, err := c.ReadPacket()
		if err != nil {
			return
		}
		if len(pk) == 0 || pk[0] != 0xfe {
			continue
		}
		body := pk[1:]
		switch {
		case bytes.HasPrefix(body, []byte("BIG")):
			c.Write(append([]byte{0xfe}, pattern(300000)...))
		case bytes.HasPrefix(body, []byte("COUNT")):
			n := int(binary.BigEndian.Uint32(body[5:]))
			for i := 0; i < n; i++ {
				out := []byte{0xfe, 'N'}
				out = binary.BigEndian.AppendUint32(out, uint32(i))
				out = append(out, pattern(i%900)...)
				c.Write(out)
			}
		default:
			c.Write(append([]byte{0xfe}, body...))
		}
	}
}

func relay(listen, target string, drop float64) {
	pc, err := net.ListenPacket("udp", listen)
	if err != nil {
		panic(err)
	}
	var mu sync.Mutex
	upstream := map[string]*net.UDPConn{}
	taddr, _ := net.ResolveUDPAddr("udp", target)
	buf := make([]byte, 4096)
	for {
		n, from, err := pc.ReadFrom(buf)
		if err != nil {
			return
		}
		mu.Lock()
		up := upstream[from.String()]
		if up == nil {
			up, _ = net.DialUDP("udp", nil, taddr)
			upstream[from.String()] = up
			go func(up *net.UDPConn, from net.Addr) {
				b := make([]byte, 4096)
				for {
					n, err := up.Read(b)
					if err != nil {
						return
					}
					// Never drop the offline handshake, only connected traffic.
					if b[0]&0x80 != 0 && rand.Float64() < drop {
						continue
					}
					pc.WriteTo(b[:n], from)
				}
			}(up, from)
		}
		mu.Unlock()
		if buf[0]&0x80 != 0 && rand.Float64() < drop {
			continue
		}
		up.Write(buf[:n])
	}
}

func main() {
	listen := flag.String("listen", "127.0.0.1:19232", "server address")
	rel := flag.String("relay", "", "lossy relay address (optional)")
	drop := flag.Float64("drop", 0.2, "drop probability of connected datagrams in the relay")
	flag.Parse()

	l, err := raknet.Listen(*listen)
	if err != nil {
		panic(err)
	}
	l.PongData([]byte("MCPE;Harness;2225;1.26.60;0;10;1234567;Perlin;Survival;1;19132;19133;"))
	if *rel != "" {
		go relay(*rel, *listen, *drop)
	}
	fmt.Println("ready")
	for {
		c, err := l.Accept()
		if err != nil {
			time.Sleep(time.Second)
			continue
		}
		go serve(c.(*raknet.Conn))
	}
}
