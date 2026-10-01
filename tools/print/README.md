# tools/print

Tools for printing from the host build of the reconstruction to a printer on
the network (`src/print/host/HostIPP.h`): the ROM's PostScript printer
(`TPSPrinter`) and HP PCL driver (`ThpPCL`) send their jobs by IPP, the
Internet Printing Protocol (RFC 8011), through the host's own network stack.

## ippprinter.py - a printer on the network to print to

A small IPP printer: it listens on 127.0.0.1, answers Print-Job (and
Validate-Job and Get-Printer-Attributes), saves every document it is sent
and logs what it got.  With a program after `--` it runs it with
`NEWTON_IPP_PRINTER=ipp://127.0.0.1:PORT/ipp/print` in its environment, which
newton takes as it takes `--ipp-printer`, and stops when the program ends.

    python tools/print/ippprinter.py [--port PORT] [--out DIR] [--path /ipp/print]
                                     [--status N] [--problem REASON[:N]] [--down]
                                     [--advertise NAME [--formats ps,pcl]]
                                     [-- <program> [args...]]

**Inputs:** `--port` (default 0: a free one, so runs from two build
directories never meet), `--out` the directory the documents go to (default
the working directory), `--path` the printer's path (default `/ipp/print`),
`--status` the IPP status-code to answer a Print-Job with (default 0,
successful-ok; `0x040a` refuses the document's format, for trying a refusal;
`0x0507` is busy), `--problem REASON[:N]` the printer stopped with that
printer-state-reason (`media-empty`, `media-jam`, `door-open`,
`marker-supply-empty`, `offline`...) for its first N answers to
Get-Printer-Attributes (default 2) and then well again, `--down` nobody at
the printer's address (the port taken and let go, so connecting is refused),
`--advertise NAME` makes the program find the printer on the network under
that name instead of being given it: `NEWTON_FOUND_PRINTERS=NAME|URI|FORMATS`
replaces `NEWTON_IPP_PRINTER`, and the host's DNS-SD layer
(`src/print/host/dnssd/HostDNSSD.h`) takes the list in place of a browse;
`--formats` is what it says it takes (default `ps,pcl`).

**Outputs:** each job as `DIR/job-N.ps` (application/postscript),
`job-N.pcl` (application/vnd.hp-pcl) or `job-N.bin`; `[ipp] ...` log lines
(the operation, its attributes, what was saved, the answer) interleaved with
the program's output; the program's exit status.  Without a program it
serves until interrupted - e.g. `python tools/print/ippprinter.py --port 6310`
and then `newton --ipp-printer ipp://127.0.0.1:6310/ipp/print`.

It reads chunked and Content-Length requests and answers HTTP/1.1 with a
Content-Length.  Used by ctests `host.NewtonIPPPostScript` and
`host.NewtonIPPPCL` (`src/host/demo/print-ipp.ns`), and with `--advertise`
by `host.NewtonPrintNetworkChooser` and `host.NewtonPrintAddPrinter`
(`print-network.ns`, `print-addprinter.ns`,
`print-removeprinter.ns`), and with `--status 0x040a`, `--down` and `--problem` by
`host.NewtonIPPRefused`, `host.NewtonIPPPCLRefused`, `host.NewtonIPPDown` and
`host.NewtonIPPProblem` (`print-ipp-refused.ns` and its kin).

## jobcheck.py - check a job a Newton printed

    python tools/print/jobcheck.py JOB [--pages N] [--text TEXT]...
                                   [--inked PAGE:LEFT,TOP,RIGHT,BOTTOM[:MIN]]...
                                   [--png OUT]

**Input:** a document `ippprinter.py` saved; its kind is told from its first
bytes.

* **PostScript:** the DSC structure (`%!PS-Adobe-3.0` first, `%%EndProlog`
  before the first page, `%%BeginSetup`/`%%EndSetup`, `%%Page: N N` and a
  `showpage` for each of `--pages` pages, `%%Pages: N` in the trailer,
  `%%EOF`), and each `--text` shown as `(TEXT) show` (or `awidthshow`), with
  `(`, `)` and `\` escaped as the driver escapes them.
* **PCL:** the universal exit and reset first; every escape sequence parsed
  (combined ones such as `ESC *b2m35W` included); 300 dots an inch; each
  page's raster from `ESC *r0A` to `ESC *rC` decoded - `ESC *b<n>Y` skips,
  `ESC *b<n>W` rows, PackBits when the row says `2m` - into a bitmap that
  must not be blank; each `--inked` rectangle (in the page's dots, 300 an
  inch) must have at least MIN black dots (default 200); `--png OUT` writes
  each page as `OUT-N.png` (one bit, `tools/imaging/png.py`) to look at.

**Output:** what was found, then `jobcheck: passed`, or the first thing wrong
and exit status 1.  Used by ctests `host.NewtonIPPPostScript.check` and
`host.NewtonIPPPCL.check`.
