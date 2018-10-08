.PHONY: all clean install run love

RAKE = rake

all:
	$(RAKE)

clean:
	$(RAKE) clobber

install:
	$(RAKE) install

run:
	$(RAKE) run
