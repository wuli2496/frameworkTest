SUBDIRS = $(sort $(subst /,, $(dir $(wildcard */*))))

INC = $(foreach dir, $(SUBDIRS), -I $(dir))

CC = g++

CPPFLAGS += -I ./
CPPFLAGS += $(INC)

TARGET_SRC = $(foreach dir, $(SUBDIRS), $(wildcard $(dir)/*.cpp))
TARGET_SRC += $(wildcard *.cpp)
TARGET_OBJ = $(TARGET_SRC:.cpp=.o)
TARGET_EXE = test

RM = rm -rf

all: PRE_MAKE $(TARGET_EXE)

PRE_MAKE:
	@echo $(SUBDIRS)
	@echo $(INC)
	@echo $(TARGET_SRC)

$(TARGET_EXE):$(TARGET_OBJ)
	$(CC) -o $@ $(TARGET_OBJ) $(CPPFLAGS)

clean:
	$(RM) $(TARGET_OBJ)
