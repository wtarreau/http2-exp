This is a mini HPACK encoder to experiment with HTTP/2.

I *hope* it follows the standard. It did my best to ensure it was OK,
so any error could have slipped through. The "never indexed" encoding
was not implemented because I didn't know what header to apply it to.

Build with "make".

Run Mark's fake-hdrs.py to produce a file (so that all variations are tested
on identical data) :

     ./fake-hdrs.py > test.hdrs

Then feed this output file to mini-enc and compare the statistics for various
encodings. The following encoders are available :

   - draft-09 : no option
   - first proposal "option 3" : "-1"
   - updated proposal for "option 3" : "-2"
   - Greg's following proposal : "-3"

   ./mini-enc    < test.hdrs
   ./mini-enc -1 < test.hdrs
   ./mini-enc -2 < test.hdrs
   ./mini-enc -3 < test.hdrs

WARNING: Never ever reuse this code for a real implementation, it's dirty
         and was written quickly for experimentation. It lacks any form of
         bounds checking and definitely is insecure.

Current results : here we only look at the overall compression ratio and
the average number of bytes per integer since we only play with integer
encoding. All measures are also reported as percent compared to draft-09.

Draft-09 :
	Total input bytes : 7455384
	Total output bytes : 2318395            (100%)
	Overall compression ratio : 0.310969    (100%)
	Total encoded integers: 218865
	Total encoded integers bytes: 295036    (100%)
	Avg bytes per integers: 1.348027        (100%)

-1 :
	Total input bytes : 7455384
	Total output bytes : 2268350            (97.84%)
	Overall compression ratio : 0.304257    (97.84%)
	Total encoded integers: 218865
	Total encoded integers bytes: 244991    (83.03%)
	Avg bytes per integers: 1.119370        (83.03%)

-2 :
	Total input bytes : 7455384
	Total output bytes : 2264722            (97.68%)
	Overall compression ratio : 0.303770    (97.68%)
	Total encoded integers: 218865
	Total encoded integers bytes: 241363    (81.81%)
	Avg bytes per integers: 1.102794        (81.81%)

-3 :
	Total input bytes : 7455384
	Total output bytes : 2280713            (98.37%)
	Overall compression ratio : 0.305915    (98.37%)
	Total encoded integers: 218865
	Total encoded integers bytes: 257354    (87.23%)
	Avg bytes per integers: 1.175857        (87.23%)

The integer encoding is 17-18% smaller on average with the new proposal
and its revised version. This means the savings can definitely be good
for headers with shorter values (eg: IP addresses). The current test set
contains average headers. The overall savings compared to draft-09 on
this test set is 2.1 to 2.3% depending on the proposal. Greg's proposal
saves a bit less, probably because it leaves extra bits for already
large data and a bit less for small data. But it definitely improves
the situation compared to draft-09.

