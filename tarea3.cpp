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

// Estructuras de datos
struct Page {
    int process_id;
    int page_number;
    bool in_ram; // true = RAM, false = SWAP
    int frame_number; // Si está en RAM, en qué marco físico
};

struct Process {
    int id;
    int size_mb;
    std::vector<int> page_table_indices; // Índices de sus páginas en la tabla global
};

class MemorySimulator {
private:
    // Configuración
    long long total_ram_size; // Bytes
    long long total_swap_size; // Bytes
    int page_size; // Bytes
    int min_proc_size, max_proc_size; // MB

    // Estado de la memoria
    int total_frames_ram;
    int total_pages_swap;
    
    // Tablas de páginas
    // Tabla global de páginas: mapea un ID único de página a su estructura
    std::vector<Page> global_page_table;
    
    // Lista de marcos libres en RAM
    std::list<int> free_frames;
    
    // Cola para política FIFO en RAM (guarda el ID único de la página)
    std::queue<int> ram_fifo_queue;

    // Gestión de SWAP
    int used_swap_pages = 0;

    // Lista de procesos activos
    std::vector<Process> processes;
    int next_process_id = 1;

    // Generador de aleatorios
    std::mt19937 rng;

public:
    MemorySimulator(int ram_mb, int p_size_kb, int p_min, int p_max) {
        rng.seed(std::random_device()());
        
        page_size = p_size_kb * 1024;
        total_ram_size = (long long)ram_mb * 1024 * 1024;
        min_proc_size = p_min;
        max_proc_size = p_max;

        // Calcular Memoria Virtual (1.5 a 4.5 veces la física) 
        std::uniform_real_distribution<double> dist(1.5, 4.5);
        double multiplier = dist(rng);
        long long total_virtual_mem = (long long)(total_ram_size * multiplier);
        
        // La memoria Swap es la diferencia o el total disponible para paginar fuera
        total_swap_size = total_virtual_mem - total_ram_size; 
        if (total_swap_size < 0) total_swap_size = 0;

        total_frames_ram = total_ram_size / page_size;
        total_pages_swap = total_swap_size / page_size;

        // Inicializar marcos libres
        for (int i = 0; i < total_frames_ram; ++i) {
            free_frames.push_back(i);
        }

        std::cout << "=== Configuración Inicial ===" << std::endl;
        std::cout << "RAM Física: " << ram_mb << " MB (" << total_frames_ram << " marcos)" << std::endl;
        std::cout << "Memoria Virtual Total: " << (total_virtual_mem / (1024*1024)) << " MB" << std::endl;
        std::cout << "Swap Disponible: " << (total_swap_size / (1024*1024)) << " MB (" << total_pages_swap << " páginas)" << std::endl;
        std::cout << "Tamaño de Página: " << p_size_kb << " KB" << std::endl;
        std::cout << "-----------------------------" << std::endl;
    }

    // Retorna falso si no hay memoria (condición de término)
    bool createProcess() {
        std::uniform_int_distribution<int> size_dist(min_proc_size, max_proc_size);
        int p_size_mb = size_dist(rng);
        long long p_size_bytes = (long long)p_size_mb * 1024 * 1024;
        int pages_needed = (p_size_bytes + page_size - 1) / page_size;

        std::cout << "[Crear] Proceso ID " << next_process_id << " (" << p_size_mb << " MB - " << pages_needed << " páginas)... ";

        // Verificar si cabe en SWAP + RAM (límite global)
        // Simplificación: verificamos si tenemos espacio en Swap para lo que no quepa en RAM, 
        // o si el sistema total está lleno.
        int total_pages_used = global_page_table.size();
        int total_capacity = total_frames_ram + total_pages_swap;

        if (total_pages_used + pages_needed > total_capacity) {
            std::cout << "FALLO. Memoria Virtual llena." << std::endl;
            return false; // [cite: 22] Terminar programa
        }

        Process proc;
        proc.id = next_process_id++;
        proc.size_mb = p_size_mb;

        for (int i = 0; i < pages_needed; ++i) {
            Page p;
            p.process_id = proc.id;
            p.page_number = i;
            p.in_ram = false;
            p.frame_number = -1;

            // Intentar asignar en RAM primero
            if (!free_frames.empty()) {
                p.in_ram = true;
                p.frame_number = free_frames.front();
                free_frames.pop_front();
                ram_fifo_queue.push(global_page_table.size()); // Guardar índice global para FIFO
            } else {
                // Va a SWAP [cite: 14]
                if (used_swap_pages >= total_pages_swap) {
                     std::cout << "CRÍTICO: Swap lleno durante asignación." << std::endl;
                     return false; // [cite: 15]
                }
                p.in_ram = false;
                used_swap_pages++;
            }

            // Guardar página en la tabla global y referenciarla en el proceso
            proc.page_table_indices.push_back(global_page_table.size());
            global_page_table.push_back(p);
        }

        processes.push_back(proc);
        std::cout << "OK. (RAM ocupada: " << (total_frames_ram - free_frames.size()) 
                  << "/" << total_frames_ram << ", Swap ocupada: " << used_swap_pages << ")" << std::endl;
        return true;
    }

    void killRandomProcess() {
        if (processes.empty()) return;

        std::uniform_int_distribution<int> dist(0, processes.size() - 1);
        int idx = dist(rng);
        Process proc = processes[idx];

        std::cout << "[Matar] Finalizando Proceso ID " << proc.id << "... ";

        // Liberar páginas
        for (int page_idx : proc.page_table_indices) {
            Page &p = global_page_table[page_idx];
            if (p.in_ram) {
                // Marcar marco como libre
                free_frames.push_back(p.frame_number);
                // Nota: El índice sigue en la cola FIFO, pero lo ignoraremos si lo sacamos después
                // y vemos que el proceso ya no existe (o manejamos limpieza compleja).
                // Para simplicidad, marcamos la página como "liberada" invalidando ID de proceso en tabla global.
                p.process_id = -1; 
            } else {
                used_swap_pages--;
            }
        }

        // Eliminar de la lista
        processes.erase(processes.begin() + idx);
        std::cout << "Liberado." << std::endl;
    }

    // [cite: 19, 20, 21] Simular acceso y Page Fault
    bool accessRandomVirtualAddress() {
        if (processes.empty()) return true;

        // Seleccionar proceso y página aleatoria
        std::uniform_int_distribution<int> p_dist(0, processes.size() - 1);
        Process& proc = processes[p_dist(rng)];

        std::uniform_int_distribution<int> page_dist(0, proc.page_table_indices.size() - 1);
        int local_page_idx = page_dist(rng);
        int global_idx = proc.page_table_indices[local_page_idx];
        Page& target_page = global_page_table[global_idx];

        // Calcular dirección virtual simulada
        long long virtual_addr = (long long)target_page.page_number * page_size + 123; // Offset arbitrario
        std::cout << "[Acceso] Dir. Virtual " << virtual_addr << " (Proc " << proc.id << ", Pag " << target_page.page_number << ")... ";

        if (target_page.in_ram) {
            std::cout << "HIT en RAM (Marco " << target_page.frame_number << ")." << std::endl;
        } else {
            std::cout << "PAGE FAULT. ";
            // Manejar fallo de página (Traer de Swap a RAM)
            
            int frame_to_use = -1;

            // 1. Si hay marcos libres, usar uno
            if (!free_frames.empty()) {
                frame_to_use = free_frames.front();
                free_frames.pop_front();
            } else {
                // 2. Reemplazo FIFO 
                // Buscar una página válida en la cola FIFO (algunas pueden ser de procesos muertos)
                while (!ram_fifo_queue.empty()) {
                    int victim_idx = ram_fifo_queue.front();
                    ram_fifo_queue.pop();
                    
                    Page& victim_page = global_page_table[victim_idx];
                    
                    // Si el proceso sigue vivo, hacemos swap out
                    if (victim_page.process_id != -1 && victim_page.in_ram) {
                        if (used_swap_pages >= total_pages_swap) {
                            std::cout << "ERROR CRÍTICO: No hay espacio en Swap para intercambio." << std::endl;
                            return false; // [cite: 22]
                        }
                        
                        // Swap OUT victim
                        frame_to_use = victim_page.frame_number;
                        victim_page.in_ram = false;
                        victim_page.frame_number = -1;
                        used_swap_pages++;
                        std::cout << "(Swap OUT Pag " << victim_page.page_number << " Proc " << victim_page.process_id << ") ";
                        break;
                    } else if (victim_page.process_id == -1 && victim_page.in_ram) {
                        // Página de proceso muerto que no se limpió de la cola, usar su marco
                        frame_to_use = victim_page.frame_number;
                        victim_page.in_ram = false; // Ya no está en ram
                        break;
                    }
                }
            }

            if (frame_to_use == -1) {
                std::cout << "Error irrecuperable de memoria." << std::endl;
                return false;
            }

            // Swap IN target
            target_page.in_ram = true;
            target_page.frame_number = frame_to_use;
            ram_fifo_queue.push(global_idx);
            used_swap_pages--; // Salió de swap
            
            std::cout << "-> Swap IN a Marco " << frame_to_use << "." << std::endl;
        }
        return true;
    }
};

int main() {
    int ram_mb, page_kb, p_min, p_max;

    std::cout << "--- Simulador de Memoria (Tarea 3) ---" << std::endl;
    //  Entradas
    std::cout << "Ingrese tamano de Memoria Fisica (MB): ";
    if (!(std::cin >> ram_mb)) return 1;
    
    std::cout << "Ingrese tamano de Pagina (KB): ";
    if (!(std::cin >> page_kb)) return 1;

    std::cout << "Ingrese rango de tamano de procesos (MB) [min max]: ";
    if (!(std::cin >> p_min >> p_max)) return 1;

    MemorySimulator sim(ram_mb, page_kb, p_min, p_max);

    int time_elapsed = 0;
    bool running = true;

    std::cout << "\nIniciando simulacion..." << std::endl;

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        time_elapsed++;

        // [cite: 12] Crear procesos cada 2 segundos
        if (time_elapsed % 2 == 0) {
            if (!sim.createProcess()) {
                std::cout << "Simulacion terminada por falta de memoria." << std::endl;
                break;
            }
        }

        //  A partir de los 30 segundos
        if (time_elapsed > 30) {
            // [cite: 18] Matar proceso cada 5 segundos
            if (time_elapsed % 5 == 0) {
                sim.killRandomProcess();
            }
            
            // [cite: 19] Acceso a memoria cada 5 segundos (se hace en el mismo tick para simplificar)
            if (time_elapsed % 5 == 0) {
               if (!sim.accessRandomVirtualAddress()) {
                   std::cout << "Simulacion terminada por error de paginacion." << std::endl;
                   break;
               }
            }
        }
        
        // Visualización simple del paso del tiempo
        if (time_elapsed % 5 == 0) std::cout << "--- Tiempo: " << time_elapsed << "s ---" << std::endl;
    }

    std::cout << "Fin del programa." << std::endl;
    return 0;
}
