#	$Id: rfcnum.awk,v 1.2 1997/10/04 16:23:37 mkb Exp $

BEGIN {
	print "/* Generated by rfcnum.awk, do not edit. */"
	print "/* $Id: rfcnum.awk,v 1.2 1997/10/04 16:23:37 mkb Exp $ */\n"
	print "#ifndef NUM_H"
	print "#define NUM_H\t1\n"
}
/^ +[0-9][0-9][0-9] +(ERR|RPL)/ {
	printf("#define %s\t%s\n", $2, $1)
	if (length($3) > 0)
		printf("#define %s\t%s\n", $4, $3)
}
END {
	print "\n#endif\t/* !NUM_H */\n"
}

