mugshot: mugshot.c ../str.h pgwire.c pgwire.h
	cc -fsanitize=address -gdwarf -DDEBUG -o mugshot mugshot.c pgwire.c

clean:
	rm mugshot
