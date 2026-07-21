#ifndef MBHAXE_VITA_SYS_TERMIOS_H
#define MBHAXE_VITA_SYS_TERMIOS_H

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[32];
};

int tcgetattr(int file, struct termios *term);
int tcsetattr(int file, int action, const struct termios *term);
void cfmakeraw(struct termios *term);

#define TCSANOW 0

#endif
