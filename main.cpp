#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <ncurses.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Konfiguracja symulacji
struct Config {
  int max_tables_2;
  int max_tables_4;
  int max_tables_6;

  int max_veg;
  int max_meat;
  int max_bread;
  int max_disposable;

  int max_forks;
  int max_knives;
  int max_spoons;

  int group_min_size;
  int group_max_size;
  int takeout_chance;

  int supplier_mode;
  int supplier_speed_us;
  int dish_speed_us;
  int cust_min_us;
  int cust_max_us;
};

struct SharedState {
  pthread_mutex_t mutex;

  // Zasoby
  int free_tables_2;
  int free_tables_4;
  int free_tables_6;

  int cnt_veg;
  int cnt_meat;
  int cnt_bread;
  int cnt_disposable;

  int clean_forks;
  int clean_knives;
  int clean_spoons;
  int dirty_forks;
  int dirty_knives;
  int dirty_spoons;

  // Statystyki Operacyjne
  int served_people_hall;
  int served_people_takeout;
  int rejected_groups_hall;
  int rejected_groups_takeout;
  int total_orders_hall;
  int total_orders_takeout;
  int total_washed_items;

  // Szczegółowe statystyki zużycia żywności
  int cons_veg_hall;
  int cons_veg_takeout;
  int cons_meat_hall;
  int cons_meat_takeout;
  int cons_bread_hall;
  int cons_bread_takeout;
  int cons_disposable; // Tylko na wynos

  // Logika dostawcy
  int next_veg;
  int next_meat;
  int next_bread;
  int next_disposable;
  int supplier_progress;
  bool running;
};

SharedState *state = nullptr;
Config config;

void signal_handler(int signum) {
  if (state)
    state->running = false;
}

void init_shared_memory() {
  void *mem = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    perror("Błąd mmap");
    exit(1);
  }
  state = (SharedState *)mem;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&state->mutex, &attr);

  state->free_tables_2 = config.max_tables_2;
  state->free_tables_4 = config.max_tables_4;
  state->free_tables_6 = config.max_tables_6;

  state->cnt_veg = config.max_veg;
  state->cnt_meat = config.max_meat;
  state->cnt_bread = config.max_bread;
  state->cnt_disposable = config.max_disposable;

  state->clean_forks = config.max_forks;
  state->clean_knives = config.max_knives;
  state->clean_spoons = config.max_spoons;
  state->dirty_forks = 0;
  state->dirty_knives = 0;
  state->dirty_spoons = 0;

  // Zerowanie statystyk
  state->served_people_hall = 0;
  state->served_people_takeout = 0;
  state->rejected_groups_hall = 0;
  state->rejected_groups_takeout = 0;
  state->total_orders_hall = 0;
  state->total_orders_takeout = 0;
  state->total_washed_items = 0;

  // Zerowanie liczników żywności
  state->cons_veg_hall = 0;
  state->cons_veg_takeout = 0;
  state->cons_meat_hall = 0;
  state->cons_meat_takeout = 0;
  state->cons_bread_hall = 0;
  state->cons_bread_takeout = 0;
  state->cons_disposable = 0;

  state->running = true;
  state->supplier_progress = 0;

  state->next_veg = config.max_veg / 2;
  state->next_meat = config.max_meat / 2;
  state->next_bread = config.max_bread / 2;
  state->next_disposable = config.max_disposable / 2;
}

void draw_progress_bar(int y, int x, int progress, const char *label) {
  mvprintw(y, x, "%s: [", label);
  int bar_width = 15;
  int filled = (progress * bar_width) / 100;
  attron(COLOR_PAIR(4) | A_BOLD);
  for (int i = 0; i < bar_width; i++)
    addch(i < filled ? '=' : ' ');
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
    mvprintw(1, 2, "RESTAURACJA (PID: %d) - Ctrl+C by zakonczyc", getpid());
    attroff(COLOR_PAIR(4) | A_BOLD);

    mvprintw(2, 40, "Tryb: %s",
             (config.supplier_mode == 1 ? "Staly" : "Smart"));
    draw_progress_bar(2, 60, state->supplier_progress, "Dostawa");

    pthread_mutex_lock(&state->mutex);

    mvprintw(4, 2, "=== SALA (STOLY) ===");

    mvprintw(5, 4, "2-os: ");
    int u2 = config.max_tables_2 - state->free_tables_2;
    attron(COLOR_PAIR(2));
    for (int i = 0; i < u2; i++)
      addch('X');
    attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(1));
    for (int i = 0; i < state->free_tables_2; i++)
      addch('_');
    attroff(COLOR_PAIR(1));

    mvprintw(6, 4, "4-os: ");
    int u4 = config.max_tables_4 - state->free_tables_4;
    attron(COLOR_PAIR(2));
    for (int i = 0; i < u4; i++)
      addch('X');
    attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(1));
    for (int i = 0; i < state->free_tables_4; i++)
      addch('_');
    attroff(COLOR_PAIR(1));

    mvprintw(7, 4, "6-os: ");
    int u6 = config.max_tables_6 - state->free_tables_6;
    attron(COLOR_PAIR(2));
    for (int i = 0; i < u6; i++)
      addch('X');
    attroff(COLOR_PAIR(2));
    attron(COLOR_PAIR(1));
    for (int i = 0; i < state->free_tables_6; i++)
      addch('_');
    attroff(COLOR_PAIR(1));

    mvprintw(9, 2, "=== STATYSTYKI SALA ===");
    printw("\n  Ludzie obsluzeni : %d", state->served_people_hall);
    printw("\n  Zamowienia       : %d", state->total_orders_hall);
    printw("\n  Odrzuceni        : %d", state->rejected_groups_hall);

    mvprintw(14, 2, "=== STATYSTYKI WYNOS ===");
    printw("\n  Ludzie obsluzeni : %d", state->served_people_takeout);
    printw("\n  Zamowienia       : %d", state->total_orders_takeout);
    printw("\n  Odrzuceni        : %d", state->rejected_groups_takeout);

    mvprintw(4, 40, "=== MAGAZYN ===");
    mvprintw(5, 42, "Warzywa : %d/%d", state->cnt_veg, config.max_veg);
    mvprintw(6, 42, "Mieso   : %d/%d", state->cnt_meat, config.max_meat);
    mvprintw(7, 42, "Chleb   : %d/%d", state->cnt_bread, config.max_bread);

    mvprintw(8, 42, "Jednorazowe: ");
    attron(state->cnt_disposable > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));
    printw("%d/%d", state->cnt_disposable, config.max_disposable);
    attroff(state->cnt_disposable > 0 ? COLOR_PAIR(1) : COLOR_PAIR(2));

    mvprintw(10, 40, "=== SZTUCCE (Czyste|Uzywane|Brudne) ===");
    int uf = config.max_forks - (state->clean_forks + state->dirty_forks);
    int uk = config.max_knives - (state->clean_knives + state->dirty_knives);
    int us = config.max_spoons - (state->clean_spoons + state->dirty_spoons);

    mvprintw(11, 42, "Widelce : ");
    attron(COLOR_PAIR(1));
    printw("%d ", state->clean_forks);
    attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(4));
    printw("| %d ", uf);
    attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(3));
    printw("| %d", state->dirty_forks);
    attroff(COLOR_PAIR(3));

    mvprintw(12, 42, "Noze    : ");
    attron(COLOR_PAIR(1));
    printw("%d ", state->clean_knives);
    attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(4));
    printw("| %d ", uk);
    attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(3));
    printw("| %d", state->dirty_knives);
    attroff(COLOR_PAIR(3));

    mvprintw(13, 42, "Lyzki   : ");
    attron(COLOR_PAIR(1));
    printw("%d ", state->clean_spoons);
    attroff(COLOR_PAIR(1));
    attron(COLOR_PAIR(4));
    printw("| %d ", us);
    attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(3));
    printw("| %d", state->dirty_spoons);
    attroff(COLOR_PAIR(3));

    mvprintw(15, 40, "=== POMYWACZ ===");
    mvprintw(16, 42, "Umyte lacznie: %d", state->total_washed_items);

    pthread_mutex_unlock(&state->mutex);
    refresh();
    usleep(100000);
  }
  endwin();
  exit(0);
}

void refill_resource(int &current, int max, int &next_order, int mode) {
  int to_add = (mode == 1) ? (max / 2 < 1 ? 1 : max / 2) : next_order;
  if (current < max) {
    if (current + to_add > max)
      current = max;
    else
      current += to_add;
  }
  if (mode == 2)
    next_order = max - current;
}

void process_supplier() {
  while (state->running) {
    int steps = 50;
    int step_delay = config.supplier_speed_us / steps;
    for (int i = 0; i <= steps && state->running; i++) {
      usleep(step_delay);
      state->supplier_progress = (i * 100) / steps;
    }

    pthread_mutex_lock(&state->mutex);
    refill_resource(state->cnt_veg, config.max_veg, state->next_veg,
                    config.supplier_mode);
    refill_resource(state->cnt_meat, config.max_meat, state->next_meat,
                    config.supplier_mode);
    refill_resource(state->cnt_bread, config.max_bread, state->next_bread,
                    config.supplier_mode);
    refill_resource(state->cnt_disposable, config.max_disposable,
                    state->next_disposable, config.supplier_mode);
    state->supplier_progress = 0;
    pthread_mutex_unlock(&state->mutex);
  }
  exit(0);
}

void process_dishwasher() {
  while (state->running) {
    usleep(config.dish_speed_us);
    pthread_mutex_lock(&state->mutex);
    if (state->dirty_forks > 0) {
      state->dirty_forks--;
      state->clean_forks++;
      state->total_washed_items++;
    } else if (state->dirty_knives > 0) {
      state->dirty_knives--;
      state->clean_knives++;
      state->total_washed_items++;
    } else if (state->dirty_spoons > 0) {
      state->dirty_spoons--;
      state->clean_spoons++;
      state->total_washed_items++;
    }
    pthread_mutex_unlock(&state->mutex);
  }
  exit(0);
}

void process_customers() {
  signal(SIGCHLD, SIG_IGN);
  srand(time(NULL));

  while (state->running) {
    int delay = config.cust_min_us +
                (rand() % (config.cust_max_us - config.cust_min_us + 1));
    usleep(delay);

    int group_size =
        config.group_min_size +
        (rand() % (config.group_max_size - config.group_min_size + 1));
    bool is_takeout = (rand() % 100) < config.takeout_chance;

    pthread_mutex_lock(&state->mutex);

    if (is_takeout) {
      // Logika Wynos
      bool cutlery_ok = state->cnt_disposable >= group_size;
      bool food_v1 =
          (state->cnt_meat >= group_size && state->cnt_bread >= group_size);
      bool food_v2 =
          (state->cnt_meat >= group_size && state->cnt_veg >= group_size);

      if (cutlery_ok && (food_v1 || food_v2)) {
        state->total_orders_takeout++;
        state->served_people_takeout += group_size;

        // Zużycie zasobów i zapis do statystyk
        state->cnt_disposable -= group_size;
        state->cons_disposable += group_size; // Log zużycia

        state->cnt_meat -= group_size;
        state->cons_meat_takeout += group_size; // og zużycia

        if (food_v1) {
          state->cnt_bread -= group_size;
          state->cons_bread_takeout += group_size; // Log zużycia
        } else {
          state->cnt_veg -= group_size;
          state->cons_veg_takeout += group_size; // Log zużycia
        }

        pthread_mutex_unlock(&state->mutex);
      } else {
        state->rejected_groups_takeout++;
        pthread_mutex_unlock(&state->mutex);
      }
    } else {
      // Logika Sala
      int table_type = 0;
      if (group_size <= 2 && state->free_tables_2 > 0)
        table_type = 2;
      else if (group_size <= 4 && state->free_tables_4 > 0)
        table_type = 4;
      else if (group_size <= 6 && state->free_tables_6 > 0)
        table_type = 6;

      if (table_type == 0) {
        if (group_size <= 2 && state->free_tables_4 > 0)
          table_type = 4;
        else if (group_size <= 6 && state->free_tables_6 > 0)
          table_type = 6;
      }

      int menu_type = (rand() % 2);
      bool food_ok = false;
      bool cutlery_ok = false;

      if (menu_type == 0) { // Zupa
        food_ok =
            (state->cnt_veg >= group_size && state->cnt_bread >= group_size);
        cutlery_ok = (state->clean_spoons >= group_size);
      } else { // Danie główne
        food_ok =
            (state->cnt_meat >= group_size && state->cnt_veg >= group_size);
        cutlery_ok = (state->clean_forks >= group_size &&
                      state->clean_knives >= group_size);
      }

      if (table_type > 0 && food_ok && cutlery_ok) {
        state->total_orders_hall++;
        if (table_type == 2)
          state->free_tables_2--;
        else if (table_type == 4)
          state->free_tables_4--;
        else
          state->free_tables_6--;

        if (menu_type == 0) {
          state->cnt_veg -= group_size;
          state->cons_veg_hall += group_size;
          state->cnt_bread -= group_size;
          state->cons_bread_hall += group_size;
          state->clean_spoons -= group_size;
        } else {
          state->cnt_meat -= group_size;
          state->cons_meat_hall += group_size;
          state->cnt_veg -= group_size;
          state->cons_veg_hall += group_size;
          state->clean_forks -= group_size;
          state->clean_knives -= group_size;
        }

        pthread_mutex_unlock(&state->mutex);

        pid_t pid = fork();
        if (pid == 0) {
          sleep(rand() % 3 + 2);

          pthread_mutex_lock(&state->mutex);
          if (table_type == 2)
            state->free_tables_2++;
          else if (table_type == 4)
            state->free_tables_4++;
          else
            state->free_tables_6++;

          if (menu_type == 0)
            state->dirty_spoons += group_size;
          else {
            state->dirty_forks += group_size;
            state->dirty_knives += group_size;
          }

          state->served_people_hall += group_size;
          pthread_mutex_unlock(&state->mutex);
          exit(0);
        } else if (pid < 0) {
          pthread_mutex_lock(&state->mutex);
          if (table_type == 2)
            state->free_tables_2++;
          else if (table_type == 4)
            state->free_tables_4++;
          else
            state->free_tables_6++;
          pthread_mutex_unlock(&state->mutex);
        }
      } else {
        state->rejected_groups_hall++;
        pthread_mutex_unlock(&state->mutex);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  signal(SIGINT, signal_handler);
  double temp_time;

  std::cout << "=== KONFIGURACJA RESTAURACJI ===\n\n";

  std::cout << "-- STOLY --\n";
  std::cout << "2-osobowe: ";
  std::cin >> config.max_tables_2;
  std::cout << "4-osobowe: ";
  std::cin >> config.max_tables_4;
  std::cout << "6-osobowe: ";
  std::cin >> config.max_tables_6;

  std::cout << "\n-- MAGAZYN --\n";
  std::cout << "Max Warzywa: ";
  std::cin >> config.max_veg;
  std::cout << "Max Mieso:   ";
  std::cin >> config.max_meat;
  std::cout << "Max Chleb:   ";
  std::cin >> config.max_bread;
  std::cout << "Max Jednorazowe: ";
  std::cin >> config.max_disposable;

  std::cout << "\n-- SZTUCCE METALOWE --\n";
  std::cout << "Widelce: ";
  std::cin >> config.max_forks;
  std::cout << "Noze:    ";
  std::cin >> config.max_knives;
  std::cout << "Lyzki:   ";
  std::cin >> config.max_spoons;

  std::cout << "\n-- USTAWIENIA SYMULACJI --\n";

  std::cout << "Tryb Dostawcy (1=Staly, 2=Smart): ";
  std::cin >> config.supplier_mode;
  if (config.supplier_mode != 1 && config.supplier_mode != 2)
    config.supplier_mode = 1;

  std::cout << "Co ile przyjezdza dostawca (sekundy): ";
  std::cin >> temp_time;
  config.supplier_speed_us = (int)(temp_time * 1000000);

  std::cout << "Czas mycia 1 naczynia (sekundy): ";
  std::cin >> temp_time;
  config.dish_speed_us = (int)(temp_time * 1000000);

  std::cout << "Klienci co (min sekund): ";
  std::cin >> temp_time;
  config.cust_min_us = (int)(temp_time * 1000000);

  std::cout << "Klienci co (max sekund): ";
  std::cin >> temp_time;
  config.cust_max_us = (int)(temp_time * 1000000);

  std::cout << "Szansa na wynos (0-100%): ";
  std::cin >> config.takeout_chance;

  // Walidacja podstawowa
  config.group_min_size = 1;
  config.group_max_size = 6;
  if (config.max_tables_2 < 0)
    config.max_tables_2 = 2;
  if (config.max_disposable <= 0)
    config.max_disposable = 10;
  if (config.supplier_speed_us <= 0)
    config.supplier_speed_us = 4000000;

  init_shared_memory();

  pid_t pid_vis = fork();
  if (pid_vis == 0)
    process_visualizer();
  pid_t pid_sup = fork();
  if (pid_sup == 0)
    process_supplier();
  pid_t pid_dish = fork();
  if (pid_dish == 0)
    process_dishwasher();

  pid_t pid_gen = fork();
  if (pid_gen == 0) {
    process_customers();
    exit(0);
  }

  while (state->running) {
    sleep(1);
  }

  waitpid(pid_vis, NULL, 0);
  waitpid(pid_sup, NULL, 0);
  waitpid(pid_dish, NULL, 0);
  waitpid(pid_gen, NULL, 0);

  // Raport koncowy
  std::cout << "\n\n";
  std::cout << "===============================================\n";
  std::cout << "          RAPORT KONCOWY SYMULACJI             \n";
  std::cout << "===============================================\n\n";

  std::cout << "1. RUCH I ZAMOWIENIA:\n";
  std::cout << "---------------------\n";
  std::cout << std::left << std::setw(15) << "Typ" << " | " << std::setw(10)
            << "Zamowien" << " | " << std::setw(10) << "Ludzi" << " | "
            << "Odrzucono\n";
  std::cout << "-----------------------------------------------\n";
  std::cout << std::left << std::setw(15) << "SALA" << " | " << std::setw(10)
            << state->total_orders_hall << " | " << std::setw(10)
            << state->served_people_hall << " | " << state->rejected_groups_hall
            << "\n";
  std::cout << std::left << std::setw(15) << "WYNOS" << " | " << std::setw(10)
            << state->total_orders_takeout << " | " << std::setw(10)
            << state->served_people_takeout << " | "
            << state->rejected_groups_takeout << "\n";
  std::cout << "-----------------------------------------------\n";
  std::cout << std::left << std::setw(15) << "SUMA" << " | " << std::setw(10)
            << (state->total_orders_hall + state->total_orders_takeout) << " | "
            << std::setw(10)
            << (state->served_people_hall + state->served_people_takeout)
            << " | "
            << (state->rejected_groups_hall + state->rejected_groups_takeout)
            << "\n\n";

  std::cout << "2. ZUZYCIE PRODUKTOW (Ile zjedzono):\n";
  std::cout << "------------------------------------\n";
  std::cout << std::left << std::setw(15) << "Produkt" << " | " << std::setw(10)
            << "Sala" << " | " << std::setw(10) << "Wynos" << " | "
            << "RAZEM\n";
  std::cout << "-----------------------------------------------\n";
  std::cout << std::left << std::setw(15) << "Warzywa" << " | " << std::setw(10)
            << state->cons_veg_hall << " | " << std::setw(10)
            << state->cons_veg_takeout << " | "
            << (state->cons_veg_hall + state->cons_veg_takeout) << "\n";
  std::cout << std::left << std::setw(15) << "Mieso" << " | " << std::setw(10)
            << state->cons_meat_hall << " | " << std::setw(10)
            << state->cons_meat_takeout << " | "
            << (state->cons_meat_hall + state->cons_meat_takeout) << "\n";
  std::cout << std::left << std::setw(15) << "Chleb" << " | " << std::setw(10)
            << state->cons_bread_hall << " | " << std::setw(10)
            << state->cons_bread_takeout << " | "
            << (state->cons_bread_hall + state->cons_bread_takeout) << "\n";
  std::cout << std::left << std::setw(15) << "Jednorazowe" << " | "
            << std::setw(10) << "-" << " | " << std::setw(10)
            << state->cons_disposable << " | " << state->cons_disposable
            << "\n";
  std::cout << "-----------------------------------------------\n\n";

  std::cout << "3. KUCHNIA:\n";
  std::cout << "-----------\n";
  std::cout << "  - Lacznie umyto sztuccow: " << state->total_washed_items
            << "\n";
  std::cout << "===============================================\n";

  pthread_mutex_destroy(&state->mutex);
  munmap(state, sizeof(SharedState));

  return 0;
}