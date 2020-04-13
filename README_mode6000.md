# Mode 6000 README

## Introduction

FreeDV mode 6000 is an FSK mode for use with FM radios.
It has a symbol rate of 6000 baud.
The mode uses frames of 120ms.
A frame can contain voice (codec2 mode 3200) and a small amount of data, or just data.


## Symbols and bits

Symbols have a raised cosine shape.
A logical one will be represented by a symbol of the same sign as the previous symbol.
A logical zero will be represented by a sign inverted symbol compared to the previous symbol.
As a result zeroes and ones can be distinguished even when the signal gets inverted during transit.
All bits, except the sync bits, will be scrambled to reduce long runs of ones or zeroes.


## Numbers

Field           | second  | frame
----------------|---------|------
baud            | 6000    | 720
sync bits       |  133.33 |  16
----------------|---------|------
voice bits      | 5400    | 648
used voice bits | 3200    | 384
codec2 frames   |   50    |   6
extra data bits |  400    |  48
control bits    |   58.33 |   7
reserved bits   |    8.33 |   1
----------------|---------|------
data frame bits | 5733.33 | 688
control bits    |   83.33 |  10
reserved bits   |   50    |   6


## Voice frame

A voice frame contains besides voice data an additional (small) amount of bits used for the data channel.
This amount is enough to send identification within a single frame.
(e.g. by encoding a callsign according to http://dmlinking.net/eth_ar.html)
It can also be used to send larger packets using multiple frames.


## Data frame

When no voice data needs to be send the full frame can be used by the data channel.
This can be used in between voice messages, or mixed when no voice activity is detected.

