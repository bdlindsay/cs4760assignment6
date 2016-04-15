CC = gcc
CFLAGS = -g
RM = rm

EXEM = oss 
EXES = userProcess 
SRCSM = oss.c
SRCSS = userProcess.c 
OBJSM = ${SRCSM:.c=.o}
OBJSS = ${SRCSS:.c=.o}

.c:.o
	$(CC) $(CFLAGS) -c $<

all : $(EXEM) $(EXES) #$(EXEPS)

$(EXEM) : $(OBJSM)
	$(CC) -o $@ $(OBJSM) -pthread

$(OBJSM) : oss.h 

$(EXES) : $(OBJSS)
	$(CC) -o $@ $(OBJSS) -pthread

$(OBJSS) : oss.h 

clean :
	$(RM) -f $(EXES) $(EXEM) $(OBJSS) $(OBJSM) endStats.txt




