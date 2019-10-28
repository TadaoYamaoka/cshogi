from cshogi import KIF

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("path")
args = parser.parse_args()

kif = KIF.Parser.parse_file(args.path)
print(kif)