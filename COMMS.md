# Comms

Who calls whom, and which path the call uses. `linux_main` feeds two fifos. `peer_main` owns the PRU0 RPMsg device. `erpc_line` owns the Zephyr calls.

```mermaid
flowchart LR
  linux["linux_main"]
  peer["peer_main"]
  erpc["erpc_line"]
  chunk["do_chunk"]
  say["say"]
  arm["armPru1"]
  xy["sendXy"]
  take["takeChunk"]
  shm["pb2_shm_publish"]
  pru0["PRU0 main"]
  pru1["PRU1 main"]
  watch["watch_main / xy_main"]

  linux -->|"fifo pru0.in, send_to"| peer
  peer -->|"RPMsg text"| pru0
  pru0 -->|"RPMsg text, send_bytes"| peer
  linux -->|"fifo zephyr.in, send_to"| erpc
  erpc -->|"eRPC, pb2_erpc_say"| say
  erpc -->|"eRPC, pb2_erpc_arm"| arm
  erpc -->|"eRPC, pb2_erpc_xy"| xy
  erpc --> chunk
  chunk -->|"eRPC, pb2_erpc_take"| take
  arm --> shm
  xy --> shm
  shm -->|"DDR mailbox, PRU0 polls"| pru0
  pru0 -->|"shared RAM, publish_xy / arm_pru1"| pru1
  pru1 -->|"shared RAM, map_phys"| watch
```

PRU0 announces channel `rpmsg-raw` port 30. Zephyr announces `rpmsg-raw` address 1024. `say` returns its string on that eRPC channel and does not touch the PRUs. PRU1 samples shared RAM. Nothing calls it.

What is written, where it lands, and who reads it.

```mermaid
flowchart LR
  peer["peer_main"]
  pru0["PRU0 main"]
  chunk["do_chunk"]
  bulk["pb2_bulk 0x9D9EF000"]
  take["takeChunk"]
  shm["pb2_shm_publish"]
  box["pb2_shm 0x9D9FF000"]
  arm["pb2_arm 0x30050000"]
  xy["pb2_xy 0x30050040"]
  pru1["PRU1 main"]
  watch["watch_main / xy_main"]

  peer -->|"text: xy, arm, or other"| pru0
  pru0 -->|"reply_xy / reply_arm / reply_echo"| peer
  chunk -->|"bytes, length, crc, seq"| bulk
  bulk --> take
  shm -->|"cmd text, seq"| box
  box --> pru0
  pru0 -->|"period, count, seq"| arm
  pru0 -->|"x, y, seq"| xy
  arm --> pru1
  xy --> pru1
  pru1 -->|"tick, done, applied_seq, applied_period"| arm
  pru1 -->|"consumed"| xy
  arm --> watch
  xy --> watch
```

`do_chunk`, `watch_main`, and `xy_main` use `map_phys` on `/dev/mem`. `takeChunk` and `pb2_shm_publish` use the same DDR addresses from the M4. `publish_xy` and `arm_pru1` use the PRU local addresses of that shared RAM.

Zephyr SPI2 stays on the M4. The boot path also leaves a status string in the mailbox PRU0 already polls.

```mermaid
flowchart LR
  erpc["erpc_line"]
  spiWrite["spiWrite"]
  hello["pb2_spi_hello"]
  write["pb2_spi2_write"]
  spi2["spi_write main_spi2"]
  shm["pb2_shm_publish"]

  erpc -->|"eRPC, pb2_erpc_spi"| spiWrite
  spiWrite --> write
  hello --> write
  write --> spi2
  hello -->|"status text"| shm
```

`main` calls `pb2_spi_hello` once at boot. `spi_hello` in `pb2.c` writes SPI0 on its own.
