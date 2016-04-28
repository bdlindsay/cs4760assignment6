CC = gcc
CFLAGS = -g
RM = rm

EXEM = oss 
EXES = userProcess 
SRCSM = oss.c
SRCSS = userProcess.c 
OBJSM = ${SRCSM:.c=.o}
OBJSS = ${SRCSS:.c=.o}
INFOS = *.txt

.c:.o
	$(CC) $(CFLAGS) -c $<

all : $(EXEM) $(EXES) #$(EXEPS)

$(EXEM) : $(OBJSM)
	$(CC) $(CFLAGS) -o $@ $(OBJSM) -pthread

$(OBJSM) : oss.h 

$(EXES) : $(OBJSS)
	$(CC) $(CFLAGS) -o $@ $(OBJSS) -pthread

$(OBJSS) : oss.h 

clean :
	$(RM) -f $(EXES) $(EXEM) $(OBJSS) $(OBJSM) $(INFOS)




