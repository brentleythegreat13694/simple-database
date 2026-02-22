# Simple Database in C

A lightweight, file-based database implementation in C with persistent storage capabilities.

## About This Project

I built this database engine while learning C programming and low-level systems concepts. I used AI (Claude) to help debug errors and understand advanced topics, but I wrote the code, fixed the bugs, and learned how databases work at a fundamental level.

## Features

- Create and manage database tables
- Insert and select operations
- File-based persistence with fsync durability
- Simple SQL-like command interface
- B-Tree indexing for efficient storage and retrieval
- Memory paging system (4KB pages)
- Row-based storage with serialization
- Advisory file locking to prevent concurrent write corruption
- Robust I/O with partial write handling and signal interrupts
- Parent pointer tracking for B-tree navigation
- Zero-fill on short reads for data integrity

## What I Learned

- Memory management in C
- File I/O operations and system calls
- Data serialization/deserialization
- Paging systems for efficient memory use
- How databases store data persistently
- Debugging complex C programs
- Working with pointers and low-level memory

## Platform Compatibility

Currently supports **Linux systems**. Windows users can run this program using **WSL** (Windows Subsystem for Linux).

## Installation Guide

### For Windows Users: Install WSL

WSL (Windows Subsystem for Linux) allows you to run a Linux environment directly on Windows.

1. Open **Command Prompt** as Administrator
2. Run the following command:
```bash
wsl --install
```

3. Restart your computer when prompted

### Choose a Linux Distribution

After installing WSL, select a Linux distribution. I recommend **Fedora** or **Ubuntu**.

**Install Fedora:**
```bash
wsl --install -d Fedora
```

**Install Ubuntu:**
```bash
wsl --install -d Ubuntu
```

### For Linux Users

If you're already on Linux, skip the WSL installation and proceed directly to installing the build tools.

## Installing Build Tools

You need the **GCC compiler** to compile C code into an executable program.

**For Fedora:**
```bash
sudo dnf install gcc
```

**For Ubuntu/Debian:**
```bash
sudo apt install gcc
```

**Optional but recommended** - Update your system first:

**Fedora:**
```bash
sudo dnf update
```

**Ubuntu/Debian:**
```bash
sudo apt update && sudo apt upgrade
```

## Building the Program

1. Clone this repository or download the source code
2. Navigate to the directory containing `db.c`
3. Compile the program:
```bash
gcc db.c -o db
```

This creates an executable file called `db`.

## Usage

### Starting the Database

Run the program with a database filename:
```bash
./db mydatabase.db
```

The database file will be created automatically if it doesn't exist.

### Available Commands

**Meta Commands:**
- `.exit` - Exit the database (saves all data)
- `.help` - Show available commands

**SQL Commands:**
- `insert <id> <username> <email>` - Insert a new row
- `select` - Display all rows in the table

### Example Session
```bash
$ ./db mydata.db
db > insert 1 alice alice@example.com
Executed.
db > insert 2 bob bob@gmail.com
Executed.
db > insert 3 charlie charlie@yahoo.com
Executed.
db > select
(1, alice, alice@example.com)
(2, bob, bob@gmail.com)
(3, charlie, charlie@yahoo.com)
Executed.
db > .exit

# Data persists between sessions
$ ./db mydata.db
db > select
(1, alice, alice@example.com)
(2, bob, bob@gmail.com)
(3, charlie, charlie@yahoo.com)
Executed.
db > .exit
```

### Creating Multiple Databases

You can create and manage multiple separate database files:
```bash
./db users.db      # User database
./db products.db   # Products database
./db orders.db     # Orders database
```

## Technical Details

### Current Schema

Each row contains:
- `id` (uint32_t) - Unique identifier
- `username` (char[33]) - Username (max 32 characters)
- `email` (char[256]) - Email address (max 255 characters)

### Architecture

- **Paging System**: Data is organized into 4KB pages for efficient memory management
- **Persistence**: All data is flushed to disk when exiting
- **Serialization**: Rows are serialized to binary format for compact storage
- **In-Memory Caching**: Pages are cached in memory and only written when necessary

### Limitations

- Maximum 100 pages
- Fixed schema (cannot create custom tables)
- Single-writer bottleneck (advisory file locking)
- B-tree splitting incomplete (scales to ~13 rows before "Table full")
- No transactions or WAL (write-ahead log)
- No read-write locks (readers blocked during writes)

## Roadmap / Future Improvements

### Completed
- [x] Add cursor abstraction for better table navigation
- [x] Implement B-Tree for faster lookups
- [x] Add file locking and durability safeguards
- [x] Implement internal node operations (find, insert, split)
- [x] Add robust I/O with fsync and ERR_INTR handling
- [x] Enforce page limits and file validation

### In Progress / Planned
- [ ] Fix B-tree split propagation (critical blocker)
- [ ] Add crash recovery with dirty flag marker
- [ ] Implement page CRC32 checksums
- [ ] Add Write-Ahead Log (WAL) for durability
- [ ] Implement read-write locks (readers don't block each other)
- [ ] Add connection timeout handling
- [ ] Add UPDATE and DELETE operations
- [ ] Support for WHERE clauses in SELECT
- [ ] Multiple table support
- [ ] Dynamic schema creation
- [ ] Client/server architecture for true concurrency

## Acknowledgments

Built with assistance from Claude AI for debugging and learning low-level database concepts.

Inspired by the ["Let's Build a Simple Database"](https://cstack.github.io/db_tutorial/) tutorial.

## License

MIT License - feel free to use this for learning purposes.

## Contributing

This is a learning project, but suggestions and improvements are welcome! Feel free to open an issue or submit a pull request.
