CC       = gcc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -g -O2
LDFLAGS  = -lpthread -lm
TARGET   = nexusdb
SRCDIR   = src
OBJDIR   = build
SOURCES  = $(wildcard $(SRCDIR)/*.c)
OBJECTS  = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SOURCES))

.PHONY: all clean test check memcheck

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "[OK] $(TARGET) built"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "[OK] Clean"

test: $(TARGET)
	@echo "=== Running Tests ==="
	./$(TARGET)
	@echo "=== Tests Complete ==="

check:
	$(CC) $(CFLAGS) -Wconversion -Wshadow -Wundef -Wformat=2 \
		-o /dev/null $(SOURCES) $(LDFLAGS) 2>&1 || true
	@echo "[OK] Check complete (warnings above are expected — this code has bugs)"

memcheck: $(TARGET)
	valgrind --leak-check=full --track-origins=yes ./$(TARGET) 2>&1 || true
	@echo "[OK] memcheck complete (errors above are expected)"
