
| Model | C | character for SQLite bind | Note |
| ------| - | ------------------------- | ---- |
| MODEL_TYPE_INT | int | i | - |
| MODEL_TYPE_ENUM | int | e | - |
| MODEL_TYPE_DATE | time_t | d | use sqlite3_*_int64 functions |
| MODEL_TYPE_DATETIME | time_t | t | use sqlite3_*_int64 functions |
| MODEL_TYPE_BOOL | bool | b | use sqlite3_*_int functions |
| MODEL_TYPE_STRING | char * | s | - |
