from cshogi import KIF

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("out")
args = parser.parse_args()

kif = KIF.Parser.parse_file(args.input)
print(kif)

out = KIF.Exporter(args.output)
