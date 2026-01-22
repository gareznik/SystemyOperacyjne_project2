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

struct Config {
    int total_tables;     
    int max_ingredients;    
    int max_cutlery;        
};

struct SharedState {
    pthread_mutex_t mutex;

    int free_tables;
    int ingredients;    
    int clean_cutlery;
    int dirty_cutlery;
    int happy_customers;  
    int rejected_customers;
    
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

    // Inicjalizacja muteksu, aby działał MIĘDZY PROCESAMI (PTHREAD_PROCESS_SHARED)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&state->mutex, &attr);
    
    // Ustawienie wartości początkowych
    state->free_tables = config.total_tables;
    state->ingredients = config.max_ingredients;
    state->clean_cutlery = config.max_cutlery;
    state->dirty_cutlery = 0;
    state->happy_customers = 0;
    state->rejected_customers = 0;
    state->running = true;
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
        clear(); // Czyszczenie ekranu

        box(stdscr, 0, 0);
        
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(1, 2, "SYMULACJA SYSTEMU RESTAURACJI (Nacisnij Ctrl+C aby zakonczyc)");
        attroff(COLOR_PAIR(4) | A_BOLD);

        pthread_mutex_lock(&state->mutex); // Blokujemy dostęp do danych (Sekcja krytyczna)
        
        mvprintw(3, 2, "Statystyki:");
        printw(" Obsluzeni: %d | Odrzuceni: %d", state->happy_customers, state->rejected_customers);

        mvprintw(5, 2, "Zasoby:");
        
        // Stoły
        mvprintw(6, 4, "[Stoly]      : ");
        int used_tables = config.total_tables - state->free_tables;
        attron(COLOR_PAIR(2));
        for(int i=0; i<used_tables; i++) addch('O' | A_BOLD); // Zajęte
        attroff(COLOR_PAIR(2));
        attron(COLOR_PAIR(1));
        for(int i=0; i<state->free_tables; i++) addch('_');   // Wolne
        attroff(COLOR_PAIR(1));
        printw(" (%d/%d)", state->free_tables, config.total_tables);

        // Składniki
        mvprintw(7, 4, "[Skladniki]  : ");
        attron(state->ingredients > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));
        for(int i=0; i<state->ingredients; i++) addch('|');
        attroff(state->ingredients > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));
        printw(" (%d)", state->ingredients);

        // Sztućce
        mvprintw(8, 4, "[Sztucce]    : ");
        attron(COLOR_PAIR(1));
        printw("Czyste: %d ", state->clean_cutlery);
        attroff(COLOR_PAIR(1));
        attron(COLOR_PAIR(3));
        printw("| Brudne: %d", state->dirty_cutlery);
        attroff(COLOR_PAIR(3));

        pthread_mutex_unlock(&state->mutex); // Odblokowujemy dostęp

        refresh();
        usleep(100000);
    }
    endwin(); // Zakończenie trybu ncurses
    exit(0);
}

void process_supplier() {
    while (state->running) {
        sleep(4); // Dostawa zajmuje czas (co 4sekundy)
        
        pthread_mutex_lock(&state->mutex);
        if (state->ingredients < config.max_ingredients) {
            int to_add = 5; // Dostarczamy 5 jednostek
            if (state->ingredients + to_add > config.max_ingredients) 
                to_add = config.max_ingredients - state->ingredients;
            
            state->ingredients += to_add;
        }
        pthread_mutex_unlock(&state->mutex);
    }
    exit(0);
}

void process_dishwasher() {
    while (state->running) {
        usleep(1500000); 
        
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
    srand(time(NULL));
    
    while (state->running) {
        usleep(rand() % 1000000 + 500000); 

        pthread_mutex_lock(&state->mutex);
        
        //czy możemy posadzić klienta? potrzebny jest stol produkty oraz sztucce
        bool can_seat = (state->free_tables > 0) && 
                        (state->ingredients > 0) && 
                        (state->clean_cutlery > 0);

        if (can_seat) {
            state->free_tables--;
            state->ingredients--;
            state->clean_cutlery--;
            
            // Tworzymy nowy proces (fork) dla klienta, który "je"
            if (fork() == 0) {
                pthread_mutex_unlock(&state->mutex); 
                sleep(rand() % 4 + 2); // Jedzenie trwa 2-5 sekund
                pthread_mutex_lock(&state->mutex);
                state->free_tables++;
                state->dirty_cutlery++; 
                state->happy_customers++;
                pthread_mutex_unlock(&state->mutex);
                exit(0); // Proces klienta kończy się
            }
            // PROCES RODZIC (Generator) kontynuuje pętlę
        } else {
            state->rejected_customers++;
        }

        pthread_mutex_unlock(&state->mutex);
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Podaj liczbe stolow: ";
    if (!(std::cin >> config.total_tables)) config.total_tables = 5;
    
    std::cout << "Podaj max ilosc skladnikow: ";
    if (!(std::cin >> config.max_ingredients)) config.max_ingredients = 10;
    
    std::cout << "Podaj max ilosc sztuccow: ";
    if (!(std::cin >> config.max_cutlery)) config.max_cutlery = 10;

    init_shared_memory();

    pid_t pid_vis = fork();
    if (pid_vis == 0) process_visualizer();

    pid_t pid_sup = fork();
    if (pid_sup == 0) process_supplier();

    pid_t pid_dish = fork();
    if (pid_dish == 0) process_dishwasher();

    process_customers();

    wait(NULL); 
    
    pthread_mutex_destroy(&state->mutex);
    munmap(state, sizeof(SharedState));
    
    return 0;
}