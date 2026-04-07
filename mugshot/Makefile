mugshot: mugshot.c ../str.h pgwire.c pgwire.h
	cc -gdwarf -fsanitize=address -DDEBUG -o mugshot mugshot.c pgwire.c

clean:
	rm mugshot
