# Makefile

OBJS      =  main.o  
SFLS      =  main.cc 

CC        =  g++
#CXXFLAGS  =  -g -I/usr/local/include/sgi-stl -pedantic -Wall -W -Werror 
CXXFLAGS  =  -g -I/usr/local/include/sgi-stl 

run:		$(OBJS)
		$(CC) $(CFLAGS) $(OBJS)  -o run -lfl -lpthread 

main.o 	: 	


clean:
		rm *.o lex.c lex.yy.c yac.c source.tab.c\
		source.output source.tab.h tok.h printout.ps core

print:		
		a2ps --font-size=8pts -E C++ --line-numbers=1 -M letter Makefile main.cc heading.H -o printout.ps

size:		
		cat *.cc *.H | wc

checkin:
		ci README Makefile main.cc heading.H 

checkout:
		co -l  README Makefile main.cc heading.H 

test:		
		./testscript



