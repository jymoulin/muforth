# This file is part of muFORTH: http://muforth.nimblemachines.com/
#
# Copyright (c) 2002-2012 David Frech. All rights reserved, and all wrongs
# reversed. (See the file COPYRIGHT for details.)

#
# It looks a bit complicated. ;-) The idea is to automagically convert from a
# list of the C names of functions that implement muforth words to a list of
# their muforth names, formatted to initialize a C array. There are a few
# exceptional cases, but mostly it's pretty straightforward.
#
# sed rocks!
#

# keep the /* filename.c */ comments
/^\/\* .*\.c/ {
i\
\

s/^\//	\//
p
}

# keep any #if* #else or #endif lines, so we can include optional sections
/^#(if|else|endif)/p

# lose lines *not* starting with "void mu_"
/^void mu_/!d

s/^void mu_//
s/(.*)\(void\);/\1/

# don't do do_colon, do_does - they are not real forth words
#/do_/d

# now we've got the name, save it in hold space
h

s/push_//
s/_carefully//
s/less/</
s/equal/=/
s/zero/0/
#s/reset/!/
s/star(_|$)/*/
s/backslash/\\\\/
s/slash/\//
s/plus/+/
s/minus/-/
s/shift_left/<</
s/shift_right/>>/
s/fetch/@/
s/^at_/@_/
s/store/!/
s/(.*)_chain/\.\1\./
s/set_(.*)_code/<\1>/
s/lbracket/[/
s/rbracket/]/
s/semicolon/;/
s/colon/:/
s/comma$/,/
s/tick/'/
s/exit/^/
s/q/?/
s/forward/>/
s/to(_|$)/>/
s/from(_|$)/>/
s/(.*)_size/#\1/

# turn foo_ to (foo)
s/((.*)_)$/\(\2\)/

s/([a-zA-Z])_([^a-zA-Z])/\1\2/g
s/([^a-zA-Z])_([a-zA-Z])/\1\2/g
s/([^a-zA-Z])_([^a-zA-Z])/\1\2/g
s/([a-zA-Z])_([a-zA-Z])/\1-\2/g

# concat hold & pattern
H
g

# output final string for array initializer
s/(.*)\n(.*)/	{ "\2", mu_\1 },/

