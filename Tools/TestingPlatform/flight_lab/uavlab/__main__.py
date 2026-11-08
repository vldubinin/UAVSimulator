from .cli import main

# Guard обов'язковий: процес живої карти (multiprocessing, spawn на Windows) заново імпортує головний
# модуль — без нього тест запустився б удруге всередині процесу карти.
if __name__ == "__main__":
    raise SystemExit(main())
