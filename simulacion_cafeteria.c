#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <omp.h>

// =======================
// DEFINICIONES GENERALES
// =======================
// Si no existe M_PI en math.h, lo definimos
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// =======================
// PARÁMETROS GENERALES
// =======================
#define T_MIN      180.0  // Tiempo total simulado en minutos
#define DT         0.25   // Paso de simulación en minutos
#define R          24     // Número de réplicas (simulaciones independientes)
#define N_CAJA     2      // Número de cajeros
#define N_HOT      2      // Número de baristas calientes
#define N_COLD     1      // Número de baristas fríos
#define UMBRAL_LEN 50     // Umbral máximo de clientes en cola antes de abandono

// Tipos de producto (puedes ajustar a tu menú)
enum Tipo { ESPRESSO=0, AMERICANO, LATTE, TEA, FRAPPE, SMOOTHIE, TIPO_COUNT };
static inline bool es_fria(int tp){ return (tp==FRAPPE || tp==SMOOTHIE); }

// =======================
// PARÁMETROS DE NEGOCIO
// =======================
// Precios por bebida (Q)
static const double PRECIOS[TIPO_COUNT] = { 15.0, 15.0, 22.0, 12.0, 26.0, 28.0 };

// Velocidades de servicio (1/tiempo_promedio en minutos)
//   Ej.: MU_CAJA = 1/0.50 ⇒ en promedio 0.5 min por cliente en caja.
static const double MU_CAJA             = 1.0/0.50;
static const double MU_HOT[TIPO_COUNT]  = { 1.0/0.7, 1.0/0.6, 1.0/1.2, 1.0/0.5, 0, 0 };
static const double MU_COLD[TIPO_COUNT] = { 0, 0, 0, 0, 1.0/1.0, 1.0/1.1 };

// Mezcla de pedidos (probabilidades simples que suman ~1)
static const double MEZCLA[TIPO_COUNT]  = { 0.18, 0.18, 0.24, 0.15, 0.15, 0.10 };

// Llegadas por minuto: base con un pico entre 60..120 min
static void llenar_lambda(double *lambda, int ticks){
    for(int i=0;i<ticks;i++){
        double t = i*DT;
        double base = 1.2, pico = 2.8;           // ajusta estos valores si quieres
        lambda[i] = (t>=60 && t<=120)? pico : base; // clientes/minuto
    }
}

// =======================
// GENERADOR DE NÚMEROS ALEATORIOS
// =======================
typedef struct { uint64_t s; } RNG;         // generador sencillo
static inline uint64_t rotl(const uint64_t x, int k){ return (x<<k)|(x>>(64-k)); }
static inline void rng_seed(RNG *r, uint64_t seed){ r->s = seed? seed : 88172645463393265ull; }
static inline uint64_t xorshift64s(RNG *r){
    uint64_t x=r->s; x^=x>>12; x^=x<<25; x^=x>>27; r->s=x; return x*2685821657736338717ull;
}
static inline double urand(RNG *r){        // número uniforme (0,1)
    return ( (xorshift64s(r)>>11) * (1.0/9007199254740992.0) );
}
static double expo(double mu, RNG *r){     // tiempo de servicio ~ Exponencial
    double u=urand(r); if(u<=0.0) u=1e-12; return -log(u)/mu;
}
static int categorical(const double *p, int n, RNG *r){ // elegir tipo de bebida
    double u=urand(r), acc=0.0; for(int i=0;i<n;i++){ acc+=p[i]; if(u<=acc) return i; } return n-1;
}
static int poisson_knuth(double lambda, RNG *r){         // # de llegadas en un paso
    if(lambda<=0) return 0;
    if(lambda>15){ // atajo rápido cuando la tasa es alta
        double mean=lambda, std=sqrt(lambda);
        double u1=urand(r), u2=urand(r);
        double z = sqrt(-2.0*log(u1))*cos(2*M_PI*u2);
        int k=(int)floor(mean + std*z + 0.5); return (k<0)?0:k;
    }
    double L=exp(-lambda), p=1.0; int k=0; do{ k++; p*=urand(r);}while(p>L); return k-1;
}

// =======================
// ESTRUCTURAS DE DATOS
// =======================
typedef struct Cliente { double t_llegada; int tipo; double t_fin_caja; } Cliente;

typedef struct Servidor {          // representa un cajero o barista
    bool   ocupado;                // está atendiendo a alguien
    double t_restante;             // cuánto le falta para terminar
    Cliente c;                     // a quién atiende
} Servidor;

// Cola circular con bloqueo
typedef struct { Cliente *buf; int cap, head, tail, size; omp_lock_t lock; } Cola;
static void cola_init(Cola *q, int cap){
    q->buf=(Cliente*)malloc(sizeof(Cliente)*cap); q->cap=cap; q->head=q->tail=q->size=0; omp_init_lock(&q->lock);
}
static void cola_free(Cola *q){ free(q->buf); omp_destroy_lock(&q->lock); }
static inline bool cola_empty(Cola *q){ return q->size==0; }
static inline int  cola_len(Cola *q){ return q->size; }
static bool cola_enqueue(Cola *q, Cliente c){
    bool ok=false; omp_set_lock(&q->lock);
    if(q->size<q->cap){ q->buf[q->tail]=c; q->tail=(q->tail+1)%q->cap; q->size++; ok=true; }
    omp_unset_lock(&q->lock); return ok;
}
static bool cola_dequeue(Cola *q, Cliente *out){
    bool ok=false; omp_set_lock(&q->lock);
    if(q->size>0){ *out=q->buf[q->head]; q->head=(q->head+1)%q->cap; q->size--; ok=true; }
    omp_unset_lock(&q->lock); return ok;
}

// =======================
// PROTOTIPOS (para tus compas)
// =======================
static void seccion_cajas(double t, RNG *rng, Cola *q_caja, Cola *q_hot, Cola *q_cold,
                          Servidor *cajas, int n_caja, double *espera_local);
static void seccion_barra_caliente(double t, RNG *rng, Cola *q_hot, Servidor *hot, int n_hot,
                                   double *ventas_local, int *compl_local, double *espera_local);
static void seccion_barra_fria(double t, RNG *rng, Cola *q_cold, Servidor *cold, int n_cold,
                               double *ventas_local, int *compl_local, double *espera_local);

// =======================
// IMPLEMENTACIONES
// =======================
static void seccion_cajas(double t, RNG *rng, Cola *q_caja, Cola *q_hot, Cola *q_cold,
                          Servidor *cajas, int n_caja, double *espera_local)
{
    double espera_add = 0.0;

    for(int i=0; i<n_caja; ++i){
        // Avanzar servicio si ocupado
        if(cajas[i].ocupado){
            cajas[i].t_restante -= DT;
            if(cajas[i].t_restante <= 0.0){
                // Pasa a barra correspondiente
                Cliente c = cajas[i].c;
                c.t_fin_caja = t;
                if(es_fria(c.tipo)) (void)cola_enqueue(q_cold, c);
                else                (void)cola_enqueue(q_hot,  c);

                cajas[i].ocupado = false;
                cajas[i].t_restante = 0.0;
            }
        }

        // Si está libre, tomar de la cola de caja
        if(!cajas[i].ocupado){
            Cliente c;
            if(cola_dequeue(q_caja, &c)){
                espera_add += (t - c.t_llegada);
                cajas[i].c = c;
                cajas[i].t_restante = expo(MU_CAJA, rng);
                cajas[i].ocupado = true;
            }
        }
    }

    // Acumular la espera de esta sección de forma segura
    #pragma omp critical(accum_espera)
    {
        *espera_local += espera_add;
    }
}

static void seccion_barra_caliente(double t, RNG *rng, Cola *q_hot, Servidor *hot, int n_hot,
                                   double *ventas_local, int *compl_local, double *espera_local)
{
    double ventas_add = 0.0;
    int    compl_add  = 0;
    double espera_add = 0.0;

    for(int j=0; j<n_hot; ++j){
        // Avanzar si ocupado
        if(hot[j].ocupado){
            hot[j].t_restante -= DT;
            if(hot[j].t_restante <= 0.0){
                ventas_add += PRECIOS[ hot[j].c.tipo ];
                compl_add  += 1;
                hot[j].ocupado = false;
                hot[j].t_restante = 0.0;
            }
        }

        // Si libre, tomar de la cola caliente
        if(!hot[j].ocupado){
            Cliente c;
            if(cola_dequeue(q_hot, &c)){
                espera_add += (t - c.t_fin_caja);
                double mu = MU_HOT[c.tipo];
                if(mu <= 0.0) mu = 1.0; // fallback de seguridad
                hot[j].c = c;
                hot[j].t_restante = expo(mu, rng);
                hot[j].ocupado = true;
            }
        }
    }

    // Acumular de forma segura
    #pragma omp critical(accum_hot)
    {
        *ventas_local += ventas_add;
        *compl_local  += compl_add;
        *espera_local += espera_add;
    }
}

static void seccion_barra_fria(double t, RNG *rng, Cola *q_cold, Servidor *cold, int n_cold,
                               double *ventas_local, int *compl_local, double *espera_local)
{
    double ventas_add = 0.0;
    int    compl_add  = 0;
    double espera_add = 0.0;

    for(int k=0; k<n_cold; ++k){
        // Avanzar si ocupado
        if(cold[k].ocupado){
            cold[k].t_restante -= DT;
            if(cold[k].t_restante <= 0.0){
                ventas_add += PRECIOS[ cold[k].c.tipo ];
                compl_add  += 1;
                cold[k].ocupado = false;
                cold[k].t_restante = 0.0;
            }
        }

        // Si libre, tomar de la cola fría
        if(!cold[k].ocupado){
            Cliente c;
            if(cola_dequeue(q_cold, &c)){
                espera_add += (t - c.t_fin_caja);
                double mu = MU_COLD[c.tipo];
                if(mu <= 0.0) mu = 1.0; // fallback
                cold[k].c = c;
                cold[k].t_restante = expo(mu, rng);
                cold[k].ocupado = true;
            }
        }
    }

    // Acumular de forma segura
    #pragma omp critical(accum_cold)
    {
        *ventas_local += ventas_add;
        *compl_local  += compl_add;
        *espera_local += espera_add;
    }
}

// =======================
// PROGRAMA PRINCIPAL
// =======================
int main(void){
    const int TICKS = (int)floor(T_MIN/DT + 0.5);

    // Preparar las tasas de llegada por minuto (con pico a mitad de la jornada)
    double *lambda = (double*)malloc(sizeof(double)*TICKS);
    llenar_lambda(lambda, TICKS);

    // Variables globales con reducción
    double ventas_tot=0.0, espera_tot=0.0; int compl_tot=0, aband_tot=0;

    // --- Paralelismo por réplicas: cada hilo corre una simulación completa ---
    #pragma omp parallel for default(none) schedule(static) \
        shared(lambda) reduction(+:ventas_tot,espera_tot,compl_tot,aband_tot)
    for(int r_id=0; r_id<R; ++r_id){
        // Estado privado de la réplica
        RNG rng; rng_seed(&rng, 1234567ull + 7919ull*r_id);
        Cola q_caja, q_hot, q_cold; cola_init(&q_caja, 4096); cola_init(&q_hot, 4096); cola_init(&q_cold, 4096);
        Servidor cajas[N_CAJA] = {0}; Servidor hot[N_HOT] = {0}; Servidor cold[N_COLD] = {0};
        double ventas_local=0.0, espera_local=0.0; int compl_local=0, aband_local=0;

        // Tiempo avanza en pasos de DT. En cada paso corren 4 secciones en paralelo.
        for(int it=0; it<TICKS; ++it){
            double t = it*DT;

            #pragma omp parallel sections
            {
                // 1) Llegan clientes y, si las colas están enormes, algunos se van.
                #pragma omp section
                {
                    int k = poisson_knuth(lambda[it]*DT, &rng);
                    for(int j=0;j<k;j++){
                        Cliente c; c.t_llegada=t; c.t_fin_caja=0.0; c.tipo = categorical(MEZCLA, TIPO_COUNT, &rng);
                        cola_enqueue(&q_caja, c);
                    }
                    // Regla sencilla de abandono por cola muy larga
                    while(cola_len(&q_caja) > UMBRAL_LEN){ Cliente dummy; cola_dequeue(&q_caja,&dummy); aband_local++; }
                    while(cola_len(&q_hot)  > UMBRAL_LEN){ Cliente dummy; cola_dequeue(&q_hot, &dummy); aband_local++; }
                    while(cola_len(&q_cold) > UMBRAL_LEN){ Cliente dummy; cola_dequeue(&q_cold,&dummy); aband_local++; }
                }

                // 2) Cajas
                #pragma omp section
                { seccion_cajas(t, &rng, &q_caja, &q_hot, &q_cold, cajas, N_CAJA, &espera_local); }

                // 3) Barra caliente
                #pragma omp section
                { seccion_barra_caliente(t, &rng, &q_hot, hot, N_HOT, &ventas_local, &compl_local, &espera_local); }

                // 4) Barra fría
                #pragma omp section
                { seccion_barra_fria(t, &rng, &q_cold, cold, N_COLD, &ventas_local, &compl_local, &espera_local); }
            }
        }

        // Sumar resultados de esta réplica a los totales (OpenMP lo hace seguro)
        ventas_tot += ventas_local; espera_tot += espera_local;
        compl_tot  += compl_local;  aband_tot  += aband_local;

        cola_free(&q_caja); cola_free(&q_hot); cola_free(&q_cold);
    }

    // ----------------------
    // SALIDA RESUMIDA
    // ----------------------
    double prom_ventas   = ventas_tot / R;
    double prom_espera   = (compl_tot? (espera_tot/compl_tot): 0.0); // min por pedido
    double throughput    = compl_tot / (R*T_MIN);                    // pedidos/min
    double tasa_abandono = (aband_tot + compl_tot)? ((double)aband_tot/(aband_tot+compl_tot)) : 0.0;

    printf("prom_ventas=%.2f\n", prom_ventas);
    printf("prom_espera_min=%.3f\n", prom_espera);
    printf("throughput(ped/min)=%.4f\n", throughput);
    printf("tasa_abandono=%.3f\n", tasa_abandono);

    free(lambda);
    return 0;
}
