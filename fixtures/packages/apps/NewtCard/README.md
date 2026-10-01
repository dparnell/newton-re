# NewtCard-for-Newton-OS

It's a miracle this even exists.

The experience of resurrecting this software is similar to the odyssey of Mason Mark, [detailed here](https://github.com/masonmark/Dash-Board-for-Newton-OS).

Feel free to look at the code, try to run it and have fun.

Go ahead and open issues - I won't be fixing any of them. 
Seriously, it's 25 years since I've looked at the source. 
I have no idea how to get the [Newton Toolkit](https://www.macintoshrepository.org/1383-newton-toolkit) running - you'll need that to compile it.

There are a number of documents with no extension. These are probably in [WordPerfect](https://en.wikipedia.org/wiki/WordPerfect) format.

NewtCard was released in 1997. Development ended with the cancellation of the Newton in 1998.
During its short life, it won all the major software awards for Newton OS software.

George Henne
 

So whilst there seems to be an issue sorting out the source code, I found a stuffit archive inside the NS Basic 4 source folder which contained the NewtCard source which wasn't corrupt (hooray!)

Inside there I searched the source for "serial" and found the routine to check it: (x-10000017)%127=0

I generated my own serial number by choosing a number, 20, multiplying it by 127 and adding 10000017, which gave me 10002557.

And that serial, amazingly, works in both NewtCard and NS Basic to make the app work :) 

Here are 20 serial numbers generated using this method:

10000144
10000271
10000398
10000525
10000652
10000779
10000906
10001033
10001160
10001287
10001414
10001541
10001668
10001795
10001922
10002049
10002176
10002303
10002430
10002557