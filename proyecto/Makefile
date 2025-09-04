# Parámetros para estadísticas
PELOTAS ?= 400
FRAMES  ?= 100000
SEED    ?= 12345
WIDTH   ?= 960
HEIGHT  ?= 560
REPS    ?= 3
GCC     ?= gcc

apps: proyecto.exe proyecto_omp.exe

proyecto.exe: proyecto.c
	$(GCC) proyecto.c -o proyecto.exe -lgdi32 -lmsimg32 -luser32 -mwindows

proyecto_omp.exe: proyecto_omp.c
	$(GCC) proyecto_omp.c -o proyecto_omp.exe -O2 -fopenmp -lgdi32 -lmsimg32 -luser32 -mwindows

# Estadísticas headless 
estadisticas.exe: estadisticas.c
	$(GCC) estadisticas.c -O3 -fopenmp -o estadisticas.exe

# Compilar y ejecutar medición 
estadisticas: estadisticas.exe
	./estadisticas.exe -n $(PELOTAS) -frames $(FRAMES) -seed $(SEED) -width $(WIDTH) -height $(HEIGHT) -reps $(REPS)

# Limpieza
limpiar:
	-del /q proyecto.exe 2>nul || true
	-del /q proyecto_omp.exe 2>nul || true
	-del /q estadisticas.exe 2>nul || true
