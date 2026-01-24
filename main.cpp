#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <ncurses.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <csignal> 

// Konfiguracja symulacji
struct Config {
    int total_tables;      
    int max_ingredients;   
    int max_cutlery;       

    int supplier_mode;      
    int supplier_speed_us;  // Czas dostawy w mikrosekundach

    int dish_speed_us;      
    int cust_min_us;        
    int cust_max_us;        
};

struct SharedState {
    pthread_mutex_t mutex; 

    int free_tables;
    int ingredients;    
    int clean_cutlery;
    int dirty_cutlery;
    
    int happy_customers;  
    int rejected_customers;
    
    int next_order_amount; 

    // <--- NOWE: Postęp dostawy (0-100%)
    int supplier_progress; 
    
    bool running;    
};

SharedState* state = nullptr;
Config config;

void init_shared_memory() {
    void* mem = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("Błąd mmap");
        exit(1);
    }
    state = (SharedState*)mem;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&state->mutex, &attr);
    
    state->free_tables = config.total_tables;
    state->ingredients = config.max_ingredients;
    state->clean_cutlery = config.max_cutlery;
    state->dirty_cutlery = 0;
    state->happy_customers = 0;
    state->rejected_customers = 0;
    state->running = true;
    state->supplier_progress = 0; // Start od 0

    state->next_order_amount = config.max_ingredients / 2; 
}

void draw_progress_bar(int y, int x, int progress, const char* label) {
    mvprintw(y, x, "%s: [", label);
    int bar_width = 20; // Szerokość paska w znakach
    int filled = (progress * bar_width) / 100;
    
    // Kolory paska
    attron(COLOR_PAIR(4) | A_BOLD); 
    for(int i=0; i<bar_width; i++) {
        if (i < filled) addch('='); // Zapełniona część
        else addch(' ');            // Pusta część
    }
    attroff(COLOR_PAIR(4) | A_BOLD);
    
    printw("] %d%%", progress);
}

void process_visualizer() {
    initscr();     
    curs_set(0);      
    start_color();    
    use_default_colors();
    noecho();     

    init_pair(1, COLOR_GREEN, -1);
    init_pair(2, COLOR_RED, -1);
    init_pair(3, COLOR_YELLOW, -1);
    init_pair(4, COLOR_CYAN, -1);

    while (state->running) {
        erase(); 

        box(stdscr, 0, 0);
        
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(1, 2, "SYMULACJA RESTAURACJI ");
        attroff(COLOR_PAIR(4) | A_BOLD);

        mvprintw(1, 40, "Tryb Dostawcy: %s", (config.supplier_mode == 1 ? "Staly (1/2)" : "Inteligentny"));

        draw_progress_bar(3, 40, state->supplier_progress, "\t Dostawa");

        pthread_mutex_lock(&state->mutex); 
        
        mvprintw(3, 2, "Statystyki:");
        printw(" Obsluzeni: %d | Odrzuceni: %d ", state->happy_customers, state->rejected_customers);

        mvprintw(5, 2, "Zasoby:");
        
        // Stoły
        mvprintw(6, 4, "[Stoly]      : ");
        int used_tables = config.total_tables - state->free_tables;
        attron(COLOR_PAIR(2));
        for(int i=0; i<used_tables; i++) addch('X' | A_BOLD); 
        attroff(COLOR_PAIR(2));
        attron(COLOR_PAIR(1));
        for(int i=0; i<state->free_tables; i++) addch('_');   
        attroff(COLOR_PAIR(1));
        printw(" (%d/%d)", state->free_tables, config.total_tables);

        // Składniki
        mvprintw(7, 4, "[Skladniki]  : ");
        attron(state->ingredients > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));
        for(int i=0; i<state->ingredients; i++) addch('|');
        attroff(state->ingredients > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));

        // printw(" (%d) | Next Order: %d", state->ingredients, state->next_order_amount);
        printw(" (%d/%d) | Next Order: %d", state->ingredients, config.max_ingredients, state->next_order_amount);

        // Sztućce
        mvprintw(8, 4, "[Sztucce]    : ");
        attron(COLOR_PAIR(1));
        printw("Czyste: %d ", state->clean_cutlery);
        attroff(COLOR_PAIR(1));
        
        int in_use = config.max_cutlery - (state->clean_cutlery + state->dirty_cutlery);
        attron(COLOR_PAIR(4)); 
        printw("| W uzyciu: %d ", in_use);
        attroff(COLOR_PAIR(4));

        attron(COLOR_PAIR(3));
        printw("| Brudne: %d", state->dirty_cutlery);
        attroff(COLOR_PAIR(3));
        
        pthread_mutex_unlock(&state->mutex); 

        refresh();
        usleep(100000); 
    }
    endwin();
    exit(0);
}

void process_supplier() {
    while (state->running) {
        int steps = 50; 
        int step_delay = config.supplier_speed_us / steps;

        for (int i = 0; i <= steps; i++) {
            usleep(step_delay);
            state->supplier_progress = (i * 100) / steps;
        }
        
        pthread_mutex_lock(&state->mutex);
        
        int to_add = 0;
        if (config.supplier_mode == 1) {
            to_add = config.max_ingredients / 2;
            if (to_add < 1) to_add = 1;
        } else {
            to_add = state->next_order_amount;
        }

        if (state->ingredients < config.max_ingredients) {
            if (state->ingredients + to_add > config.max_ingredients) {
                state->ingredients = config.max_ingredients; 
            } else {
                state->ingredients += to_add;
            }
        }

        if (config.supplier_mode == 2) {
            int missing = config.max_ingredients - state->ingredients;
            state->next_order_amount = missing; 
        }

        state->supplier_progress = 0;

        pthread_mutex_unlock(&state->mutex);
    }
    exit(0);
}

void process_dishwasher() {
    while (state->running) {
        usleep(config.dish_speed_us); 
        
        pthread_mutex_lock(&state->mutex);
        if (state->dirty_cutlery > 0) {
            state->dirty_cutlery--;
            state->clean_cutlery++;
        }
        pthread_mutex_unlock(&state->mutex);
    }
    exit(0);
}

void process_customers() {
    signal(SIGCHLD, SIG_IGN); 
    srand(time(NULL));
    
    while (state->running) {
        int delay = config.cust_min_us + (rand() % (config.cust_max_us - config.cust_min_us + 1));
        usleep(delay); 

        pthread_mutex_lock(&state->mutex);
        
        bool can_seat = (state->free_tables > 0) && 
                        (state->ingredients > 0) && 
                        (state->clean_cutlery > 0);

        if (can_seat) {
            state->free_tables--;
            state->ingredients--;
            state->clean_cutlery--;
            
            pthread_mutex_unlock(&state->mutex); 

            pid_t pid = fork();

            if (pid == 0) {
                sleep(rand() % 3 + 2); 
                
                pthread_mutex_lock(&state->mutex);
                state->free_tables++;
                state->dirty_cutlery++; 
                state->happy_customers++;
                pthread_mutex_unlock(&state->mutex);
                
                exit(0); 
            } 
            else if (pid < 0) {
                pthread_mutex_lock(&state->mutex);
                state->free_tables++;
                state->ingredients++;
                state->clean_cutlery++;
                pthread_mutex_unlock(&state->mutex);
            }
        } else {
            state->rejected_customers++;
            pthread_mutex_unlock(&state->mutex);
        }
    }
}

int main(int argc, char* argv[]) {
    double temp_time; 

    std::cout << "=== KONFIGURACJA RESTAURACJI ===\n";
    std::cout << "(Mozesz uzywac ulamkow dla czasu, np. 0.5)\n\n";

    std::cout << "1. Liczba stolow (Domyslnie 5): ";
    std::cin >> config.total_tables;

    std::cout << "2. Pojemnosc magazynu (Skladniki) (Domyslnie 10): ";
    std::cin >> config.max_ingredients;

    std::cout << "3. Liczba sztuccow (Domyslnie 10): ";
    std::cin >> config.max_cutlery;

    std::cout << "\n--- USTAWIENIA DOSTAWCY ---\n";
    std::cout << "4. Tryb dostawcy (1 = Zawsze polowa, 2 = Inteligentny): ";
    std::cin >> config.supplier_mode;
    if (config.supplier_mode != 1 && config.supplier_mode != 2) config.supplier_mode = 1;

    std::cout << "5. Czestotliwosc dostaw (w sekundach): ";
    std::cin >> temp_time;
    config.supplier_speed_us = (int)(temp_time * 1000000); 

    std::cout << "\n--- PREDKOSC SYMULACJI ---\n";
    std::cout << "6. Czas mycia naczynia (w sekundach) (Domyslnie 1s): ";
    std::cin >> temp_time;
    config.dish_speed_us = (int)(temp_time * 1000000);

    std::cout << "7. Klienci przychodza co (min sekund) (Domyslnie 0.5s): ";
    std::cin >> temp_time;
    config.cust_min_us = (int)(temp_time * 1000000);

    std::cout << "8. Klienci przychodza co (max sekund) (Domyslnie 1s): ";
    std::cin >> temp_time;
    config.cust_max_us = (int)(temp_time * 1000000);

    if (config.total_tables <= 0) config.total_tables = 5;
    if (config.max_ingredients <= 0) config.max_ingredients = 10;
    if (config.max_cutlery <= 0) config.max_cutlery = 10;
    
    if (config.supplier_speed_us <= 0) config.supplier_speed_us = 4000000; 
    if (config.dish_speed_us <= 0) config.dish_speed_us = 1000000; 
    if (config.cust_min_us <= 0) config.cust_min_us = 500000; 
    if (config.cust_max_us < config.cust_min_us) config.cust_max_us = config.cust_min_us + 500000;

    init_shared_memory();

    if (fork() == 0) process_visualizer();
    if (fork() == 0) process_supplier();
    if (fork() == 0) process_dishwasher();

    process_customers();

    wait(NULL); 
    pthread_mutex_destroy(&state->mutex);
    munmap(state, sizeof(SharedState));
    
    return 0;
}