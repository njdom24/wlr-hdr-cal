CFLAGS = $(shell pkg-config --cflags wayland-client)
LIBS   = $(shell pkg-config --libs wayland-client)

main:
	gcc main.c -o main $(CFLAGS) $(LIBS)

clean:
	rm -f main