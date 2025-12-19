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

// --- Konfiguracja symulacji ---
struct Config {
    int total_tables;       // Liczba stołów
    int max_ingredients;    // Maksymalna liczba składników
    int max_cutlery;        // Maksymalna liczba sztućców
};

// --- Pamięć współdzielona (Stan restauracji) ---
// Ta struktura będzie znajdować się w pamięci współdzielonej (Shared Memory)
// i będzie dostępna dla wszystkich procesów.
struct SharedState {
    pthread_mutex_t mutex; // Muteks do synchronizacji procesów

    int free_tables;       // Wolne stoły
    int ingredients;       // Aktualna liczba składników
    int clean_cutlery;     // Czyste sztućce
    int dirty_cutlery;     // Brudne sztućce
    int happy_customers;   // Liczba obsłużonych klientów
    int rejected_customers;// Liczba klientów odprawionych z kwitkiem
    
    bool running;          // Flaga działania symulacji
};

// Globalne wskaźniki (będą wskazywać na obszar mmap)
SharedState* state = nullptr;
Config config;

// --- Inicjalizacja pamięci współdzielonej ---
void init_shared_memory() {
    // Alokujemy pamięć dostępną do odczytu i zapisu, współdzieloną (MAP_SHARED)
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

// --- Proces: Wizualizacja (NCurses) ---
void process_visualizer() {
    initscr();            // Inicjalizacja ncurses
    curs_set(0);          // Ukrycie kursora
    start_color();        // Włączenie kolorów
    use_default_colors();
    noecho();             // Nie pokazuj wpisywanych znaków

    // Definicja par kolorów
    init_pair(1, COLOR_GREEN, -1);  // Zasoby OK
    init_pair(2, COLOR_RED, -1);    // Braki / Błędy
    init_pair(3, COLOR_YELLOW, -1); // Ostrzeżenia / Praca w toku
    init_pair(4, COLOR_CYAN, -1);   // Nagłówki

    while (state->running) {
        clear(); // Czyszczenie ekranu
        
        // Rysowanie ramki
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

        mvprintw(10, 2, "Logi procesow:");
        mvprintw(11, 4, "Kelner sprawdza sale...");
        mvprintw(12, 4, "Zmywak pracuje...");
        mvprintw(13, 4, "Dostawca czeka na zamowienie...");

        refresh();      // Odświeżenie ekranu
        usleep(100000); // Czekamy 100ms przed kolejną klatką
    }
    endwin(); // Zakończenie trybu ncurses
    exit(0);
}

// --- Proces: Dostawca (Supplier) ---
void process_supplier() {
    while (state->running) {
        sleep(3); // Dostawa zajmuje czas (co 3 sekundy)
        
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

// --- Proces: Zmywacz (Dishwasher) ---
void process_dishwasher() {
    while (state->running) {
        usleep(1500000); // Mycie co 1.5 sekundy
        
        pthread_mutex_lock(&state->mutex);
        if (state->dirty_cutlery > 0) {
            // Myjemy jeden zestaw sztućców
            state->dirty_cutlery--;
            state->clean_cutlery++;
        }
        pthread_mutex_unlock(&state->mutex);
    }
    exit(0);
}

// --- Proces: Generator Klientów (Pętla główna) ---
// Symuluje przybycie gości i pracę kelnera
void process_customers() {
    srand(time(NULL));
    
    while (state->running) {
        usleep(rand() % 1000000 + 500000); // Klient przychodzi co 0.5 - 1.5 sekundy

        pthread_mutex_lock(&state->mutex);
        
        // Logika: Czy możemy posadzić klienta?
        // Potrzebny jest: wolny stół, składniki na danie i czyste sztućce
        bool can_seat = (state->free_tables > 0) && 
                        (state->ingredients > 0) && 
                        (state->clean_cutlery > 0);

        if (can_seat) {
            // Sadzamy klienta (pobieramy zasoby)
            state->free_tables--;
            state->ingredients--;
            state->clean_cutlery--;
            
            // Tworzymy nowy proces (fork) dla klienta, który "je"
            if (fork() == 0) {
                // PROCES DZIECKO: Klient je posiłek
                pthread_mutex_unlock(&state->mutex); // Dziecko odziedziczyło blokadę, musimy ją zwolnić!
                
                sleep(rand() % 4 + 2); // Jedzenie trwa 2-5 sekund
                
                // Klient zjadł, wychodzi
                pthread_mutex_lock(&state->mutex);
                state->free_tables++;   // Zwalniamy stół
                state->dirty_cutlery++; // Sztućce stają się brudne
                state->happy_customers++;
                pthread_mutex_unlock(&state->mutex);
                exit(0); // Proces klienta kończy się
            }
            // PROCES RODZIC (Generator) kontynuuje pętlę
        } else {
            state->rejected_customers++; // Klient odszedł niezadowolony
        }

        pthread_mutex_unlock(&state->mutex);
    }
}

int main(int argc, char* argv[]) {
    // Wczytywanie parametrów startowych
    std::cout << "Podaj liczbe stolow: ";
    if (!(std::cin >> config.total_tables)) config.total_tables = 5;
    
    std::cout << "Podaj max ilosc skladnikow: ";
    if (!(std::cin >> config.max_ingredients)) config.max_ingredients = 10;
    
    std::cout << "Podaj max ilosc sztuccow: ";
    if (!(std::cin >> config.max_cutlery)) config.max_cutlery = 10;

    // Inicjalizacja pamięci
    init_shared_memory();

    // Tworzenie procesów (fork)
    pid_t pid_vis = fork();
    if (pid_vis == 0) process_visualizer();

    pid_t pid_sup = fork();
    if (pid_sup == 0) process_supplier();

    pid_t pid_dish = fork();
    if (pid_dish == 0) process_dishwasher();

    // Główny proces staje się generatorem klientów
    process_customers();

    // Oczekiwanie na zakończenie (formalność, bo pętla jest nieskończona)
    wait(NULL); 
    
    // Sprzątanie zasobów
    pthread_mutex_destroy(&state->mutex);
    munmap(state, sizeof(SharedState));
    
    return 0;
}