#include <iostream>
#include <vector>
#include <list>
#include <map>
#include <queue>
#include <random>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>

using namespace std;

// Estructuras de datos
struct Pagina {
    int id_proceso;
    int numero_pagina;
    bool en_ram;
    int numero_marco;
};

struct Proceso {
    int id;
    int tamanio_mb;
    vector<int> indices_tabla_paginas;
};

class SimuladorMemoria {
private:
    // Configuración
    long long tamanio_total_ram; 
    long long tamanio_total_swap;
    int tamanio_pagina; 
    int tamanio_min_proc, tamanio_max_proc;

    // Estado de la memoria
    int total_marcos_ram;
    int total_paginas_swap;
    
    // Tabla global
    vector<Pagina> tabla_paginas_global;
    
    // Lista de marcos libres
    list<int> marcos_libres;
    
    // Cola para política FIFO
    queue<int> cola_fifo_ram;

    // Gestión de SWAP
    int paginas_swap_usadas = 0;

    // Lista de procesos activos
    vector<Proceso> procesos;
    int siguiente_id_proceso = 1;

    // Generador de aleatorios
    mt19937 rng;

public:
    SimuladorMemoria(int ram_mb, int p_size_kb, int p_min, int p_max) {
        rng.seed(random_device()());
        
        tamanio_pagina = p_size_kb * 1024;
        tamanio_total_ram = (long long)ram_mb * 1024 * 1024;
        tamanio_min_proc = p_min;
        tamanio_max_proc = p_max;

        // Calcular Memoria Virtual (1.5 a 4.5 veces la física)
        uniform_real_distribution<double> dist(1.5, 4.5);
        double multiplicador = dist(rng);
        long long memoria_virtual_total = (long long)(tamanio_total_ram * multiplicador);
        
        // La memoria Swap es la diferencia
        tamanio_total_swap = memoria_virtual_total - tamanio_total_ram; 
        if (tamanio_total_swap < 0) tamanio_total_swap = 0;

        total_marcos_ram = tamanio_total_ram / tamanio_pagina;
        total_paginas_swap = tamanio_total_swap / tamanio_pagina;

        // Inicializar marcos libres
        for (int i = 0; i < total_marcos_ram; ++i) {
            marcos_libres.push_back(i);
        }

        cout << "=== Configuración Inicial ===" << endl;
        cout << "RAM Física: " << ram_mb << " MB (" << total_marcos_ram << " marcos)" << endl;
        cout << "Memoria Virtual Total: " << (memoria_virtual_total / (1024*1024)) << " MB" << endl;
        cout << "Swap Disponible: " << (tamanio_total_swap / (1024*1024)) << " MB (" << total_paginas_swap << " páginas)" << endl;
        cout << "Tamaño de Página: " << p_size_kb << " KB" << endl;
        cout << "-----------------------------" << endl;
    }

    // Retorna falso si no hay memoria
    bool crearProceso() {
        uniform_int_distribution<int> dist_tamanio(tamanio_min_proc, tamanio_max_proc);
        int proc_tamanio_mb = dist_tamanio(rng);
        long long proc_tamanio_bytes = (long long)proc_tamanio_mb * 1024 * 1024;
        int paginas_necesarias = (proc_tamanio_bytes + tamanio_pagina - 1) / tamanio_pagina;

        cout << "[Crear] Proceso ID " << siguiente_id_proceso << " (" << proc_tamanio_mb << " MB - " << paginas_necesarias << " páginas)... ";

        int total_paginas_usadas = tabla_paginas_global.size();
        int capacidad_total = total_marcos_ram + total_paginas_swap;

        if (total_paginas_usadas + paginas_necesarias > capacidad_total) {
            cout << "FALLO. Memoria Virtual llena." << endl;
            return false;
        }

        Proceso nuevo_proc;
        nuevo_proc.id = siguiente_id_proceso++;
        nuevo_proc.tamanio_mb = proc_tamanio_mb;

        for (int i = 0; i < paginas_necesarias; ++i) {
            Pagina p;
            p.id_proceso = nuevo_proc.id;
            p.numero_pagina = i;
            p.en_ram = false;
            p.numero_marco = -1;

            if (!marcos_libres.empty()) {
                p.en_ram = true;
                p.numero_marco = marcos_libres.front();
                marcos_libres.pop_front();
                cola_fifo_ram.push(tabla_paginas_global.size());
            } else {
                if (paginas_swap_usadas >= total_paginas_swap) {
                     cout << "CRÍTICO: Swap lleno durante asignación." << endl;
                     return false;
                }
                p.en_ram = false;
                paginas_swap_usadas++;
            }

            nuevo_proc.indices_tabla_paginas.push_back(tabla_paginas_global.size());
            tabla_paginas_global.push_back(p);
        }

        procesos.push_back(nuevo_proc);
        cout << "OK. (RAM ocupada: " << (total_marcos_ram - marcos_libres.size()) 
             << "/" << total_marcos_ram << ", Swap ocupada: " << paginas_swap_usadas << ")" << endl;
        return true;
    }

    void matarProcesoAleatorio() {
        if (procesos.empty()) return;

        uniform_int_distribution<int> dist(0, procesos.size() - 1);
        int idx = dist(rng);
        Proceso proc = procesos[idx];

        cout << "[Matar] Finalizando Proceso ID " << proc.id << "... ";

        for (int indice_pag : proc.indices_tabla_paginas) {
            Pagina &p = tabla_paginas_global[indice_pag];
            if (p.en_ram) {
                marcos_libres.push_back(p.numero_marco);
                p.id_proceso = -1; 
            } else {
                paginas_swap_usadas--;
            }
        }

        procesos.erase(procesos.begin() + idx);
        cout << "Liberado." << endl;
    }

    bool accederDireccionVirtualAleatoria() {
        if (procesos.empty()) return true;

        uniform_int_distribution<int> dist_proc(0, procesos.size() - 1);
        Proceso& proceso = procesos[dist_proc(rng)];

        uniform_int_distribution<int> dist_pag(0, proceso.indices_tabla_paginas.size() - 1);
        int idx_local = dist_pag(rng);
        int idx_global = proceso.indices_tabla_paginas[idx_local];
        Pagina& pagina_objetivo = tabla_paginas_global[idx_global];

        long long dir_virtual = (long long)pagina_objetivo.numero_pagina * tamanio_pagina + 123; 
        cout << "[Acceso] Dir. Virtual " << dir_virtual << " (Proc " << proceso.id << ", Pag " << pagina_objetivo.numero_pagina << ")... ";

        if (pagina_objetivo.en_ram) {
            cout << "HIT en RAM (Marco " << pagina_objetivo.numero_marco << ")." << endl;
        } else {
            cout << "PAGE FAULT. ";
            
            int marco_a_usar = -1;

            if (!marcos_libres.empty()) {
                marco_a_usar = marcos_libres.front();
                marcos_libres.pop_front();
            } else {
                while (!cola_fifo_ram.empty()) {
                    int idx_victima = cola_fifo_ram.front();
                    cola_fifo_ram.pop();
                    
                    Pagina& pagina_victima = tabla_paginas_global[idx_victima];
                    
                    if (pagina_victima.id_proceso != -1 && pagina_victima.en_ram) {
                        if (paginas_swap_usadas >= total_paginas_swap) {
                            cout << "ERROR CRÍTICO: No hay espacio en Swap para intercambio." << endl;
                            return false;
                        }
                        
                        marco_a_usar = pagina_victima.numero_marco;
                        pagina_victima.en_ram = false;
                        pagina_victima.numero_marco = -1;
                        paginas_swap_usadas++;
                        cout << "(Swap OUT Pag " << pagina_victima.numero_pagina << " Proc " << pagina_victima.id_proceso << ") ";
                        break;
                    } else if (pagina_victima.id_proceso == -1 && pagina_victima.en_ram) {
                        marco_a_usar = pagina_victima.numero_marco;
                        pagina_victima.en_ram = false; 
                        break;
                    }
                }
            }

            if (marco_a_usar == -1) {
                cout << "Error irrecuperable de memoria." << endl;
                return false;
            }

            pagina_objetivo.en_ram = true;
            pagina_objetivo.numero_marco = marco_a_usar;
            cola_fifo_ram.push(idx_global);
            paginas_swap_usadas--;
            
            cout << "-> Swap IN a Marco " << marco_a_usar << "." << endl;
        }
        return true;
    }
};

int main() {
    int ram_mb, page_kb, p_min, p_max;

    cout << "--- Simulador de Memoria ---" << endl;
    cout << "Ingrese tamano de Memoria Fisica (MB): ";
    if (!(cin >> ram_mb)) return 1;
    
    cout << "Ingrese tamano de Pagina (KB): ";
    if (!(cin >> page_kb)) return 1;

    cout << "Ingrese rango de tamano de procesos (MB) [min max]: ";
    if (!(cin >> p_min >> p_max)) return 1;

    SimuladorMemoria sim(ram_mb, page_kb, p_min, p_max);

    int tiempo_transcurrido = 0;
    bool ejecutando = true;

    cout << "\nIniciando simulacion..." << endl;

    while (ejecutando) {
        this_thread::sleep_for(chrono::seconds(1));
        tiempo_transcurrido++;

        if (tiempo_transcurrido % 2 == 0) {
            if (!sim.crearProceso()) {
                cout << "Simulacion terminada por falta de memoria." << endl;
                break;
            }
        }

        if (tiempo_transcurrido > 30) {
            if (tiempo_transcurrido % 5 == 0) {
                sim.matarProcesoAleatorio();
            }
            
            if (tiempo_transcurrido % 5 == 0) {
               if (!sim.accederDireccionVirtualAleatoria()) {
                   cout << "Simulacion terminada por error de paginacion." << endl;
                   break;
               }
            }
        }
        
        if (tiempo_transcurrido % 5 == 0) cout << "--- Tiempo: " << tiempo_transcurrido << "s ---" << endl;
    }

    cout << "Fin del programa." << endl;
    return 0;
}