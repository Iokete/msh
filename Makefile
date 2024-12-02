# Variables
CC = gcc
CFLAGS = -no-pie  # Opciones de compilación (añade más si es necesario) 
LIBS = libparser_64.a   # Biblioteca a enlazar
TARGET = msh      # Nombre del ejecutable
SRC = myshell.c       # Archivo fuente

# Regla por defecto
all: $(TARGET)
run: $(TARGET)
	./$(TARGET)

# Regla para compilar el ejecutable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(TARGET)

# Limpieza de archivos generados
clean:
	rm -f $(TARGET) *.o
