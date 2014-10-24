#!/usr/bin/env python

from random import choice, sample, randrange
from string import lowercase

iterations = 10000

authorities = [
    'www.foo.com', 
    'api1.backend.region.bar.com:8000', 
    'api2.backend.region.bar.com:8000', 
    'api3.backend.region.bar.com:8000', 
    'api4.backend.region.bar.com:8000',
    'api5.backend.region.bar.com:8000'
]
custom_headers = ['x' * n for n in range(10,20)]

def fakeRequest(numCustom=10, randomPortion=.5):
  headers = []
  headers.append([':method', 'GET'])
  headers.append([':scheme', 'https'])
  headers.append([':authority', choice(authorities)])
  headers.append([':path', '/api/v1/foo/bar/baz/abcdef?%s' % randrange(1000)])
  headers.append(['user-agent', 'SomeUA/5.0 (really fake, thanks for all the fish)'])
  numRandom = int(numCustom * randomPortion)
  for custom in range(numRandom):
    headers.append([choice(custom_headers), "".join([choice(lowercase) for i in xrange(40)])])
  for custom in range(numCustom - numRandom):
    headers.append([choice(custom_headers), 'y' * 40])
  return "\n".join(["%s: %s" % (hdr[0], hdr[1]) for hdr in headers])
    
    
if __name__ == "__main__":
    for i in range(iterations):
        print fakeRequest()
        print
