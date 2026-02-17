#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* Macros and Constants */
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
#define TABLE_MAX_PAGES 100
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define PAGE_SIZE 4096

/* Data Structures */
typedef struct {
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_DUPLICATE_KEY
} ExecuteResult;

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct {
    int file_descriptor;
    uint32_t file_length;
    uint32_t num_pages;
    void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    Pager* pager;
    uint32_t root_page_num;
} Table;

typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

typedef enum {
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

/* B-Tree Layout */
#define ID_SIZE size_of_attribute(Row, id)
#define USERNAME_SIZE size_of_attribute(Row, username)
#define EMAIL_SIZE size_of_attribute(Row, email)
#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

/* Common Node Header Layout */
#define NODE_TYPE_SIZE sizeof(uint8_t)
#define NODE_TYPE_OFFSET 0
#define IS_ROOT_SIZE sizeof(uint8_t)
#define IS_ROOT_OFFSET NODE_TYPE_SIZE
#define PARENT_POINTER_SIZE sizeof(uint32_t)
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

/* Leaf Node Header Layout */
#define LEAF_NODE_NUM_CELLS_SIZE sizeof(uint32_t)
#define LEAF_NODE_NUM_CELLS_OFFSET COMMON_NODE_HEADER_SIZE
#define LEAF_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE)

/* Leaf Node Body Layout */
#define LEAF_NODE_KEY_SIZE sizeof(uint32_t)
#define LEAF_NODE_KEY_OFFSET 0
#define LEAF_NODE_VALUE_SIZE ROW_SIZE
#define LEAF_NODE_VALUE_OFFSET (LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE)
#define LEAF_NODE_CELL_SIZE (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = LEAF_NODE_MAX_CELLS / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = LEAF_NODE_MAX_CELLS - (LEAF_NODE_MAX_CELLS / 2);




/* Function Prototypes */
InputBuffer* new_input_buffer(void);
void close_input_buffer(InputBuffer* input_buffer);
void print_prompt(void);
void read_input(InputBuffer* input_buffer);
void print_row(Row* row);
void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);
ExecuteResult execute_select(Statement* statement, Table* table);
ExecuteResult execute_statement(Statement* statement, Table* table);
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement);
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);
Table* db_open(const char* filename);
void free_table(Table* table);
Pager* pager_open(const char* filename);
void* get_page(Pager* pager, uint32_t page_num);
void pager_flush(Pager* pager, uint32_t page_num);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);
Cursor* table_start(Table* table);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
uint32_t leaf_node_find_cell(void* node, uint32_t key);
uint32_t* leaf_node_num_cells(void* node);
void* leaf_node_cell(void* node, uint32_t cell_num);
uint32_t* leaf_node_key(void* node, uint32_t cell_num);
void* leaf_node_value(void* node, uint32_t cell_num);
void initialize_leaf_node(void* node);
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value);
void print_leaf_node(void* node);
uint32_t get_unused_page_num(Pager* pager);
bool is_node_root(void* node);
void set_node_root(void* node, bool is_root);
NodeType get_node_type(void* node);
void set_node_type(void* node, NodeType type);
void create_new_root(Table* table, uint32_t right_child_page_num);

/* --- REPL & Frontend Implementation --- */

void print_prompt() {
    printf("db > ");
}

InputBuffer* new_input_buffer(void) {
    InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
    // Ask the computer for a slice of memory. If the computer is "out of brain space," it returns NULL and we have to stop.
    if (input_buffer == NULL) {
        fprintf(stderr, "Error: malloc failed\n");
        exit(EXIT_FAILURE);
    }
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;
    return input_buffer;
}

// Reads input from the InputBuffer
void read_input(InputBuffer* input_buffer) {
    // Gets each line from the input_buffer
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0) {
        if (feof(stdin)) {
            printf("\nEnd of input reached.\n");
        } else {
            fprintf(stderr, "Error reading input: %s\n", strerror(errno));
        }
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
}

// Shut down input_buffer stops getting input
void close_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

// Checks input_buffer for MetaCommands
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
    // If user types .exit it will close the program and return with EXIT_SUCCESS;
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        close_input_buffer(input_buffer);
        free_table(table);
        exit(EXIT_SUCCESS);
    } else if (strcmp(input_buffer->buffer, ".help") == 0) {
        printf("Available commands:\n");
        printf(" .exit    - Exit the database\n");
        printf(" .help    - Show this help message\n");
        printf(" insert   - Insert a row (insert <id> <username> <email>)\n");
        return META_COMMAND_SUCCESS;
    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
        printf("Tree:\n");
        print_leaf_node(get_page(table->pager, 0));
        return META_COMMAND_SUCCESS;
    } else {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;

        strtok(input_buffer->buffer, " ");
        char* id_string = strtok(NULL, " ");
        char* username = strtok(NULL, " ");
        char* email = strtok(NULL, " ");

        if (id_string == NULL || username == NULL || email == NULL) {
            return PREPARE_SYNTAX_ERROR;
        }

        int id = atoi(id_string);
        if (id < 0) return PREPARE_NEGATIVE_ID;
        if (strlen(username) > COLUMN_USERNAME_SIZE) return PREPARE_STRING_TOO_LONG;
        if (strlen(email) > COLUMN_EMAIL_SIZE) return PREPARE_STRING_TOO_LONG;

        statement->row_to_insert.id = id;
        strcpy(statement->row_to_insert.username, username);
        strcpy(statement->row_to_insert.email, email);

        return PREPARE_SUCCESS;
    }

    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

/* --- Execution Logic --- */

// Executes our statement execute_statement which will execute our statements for example if its insert then execute insert statement and if its select execute select statement
ExecuteResult execute_statement(Statement* statement, Table* table) {
    switch (statement->type) {
        case (STATEMENT_INSERT): {
            void* node = get_page(table->pager, table->root_page_num);
            uint32_t num_cells = *leaf_node_num_cells(node);

            if (num_cells >= LEAF_NODE_MAX_CELLS) {
                return EXECUTE_TABLE_FULL;
            }

            Row* row_to_insert = &(statement->row_to_insert);
            uint32_t key_to_insert = row_to_insert->id;
            Cursor* cursor = leaf_node_find(table, table->root_page_num, key_to_insert);

            // Check for duplicate key
            if (cursor->cell_num < num_cells) {
                uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
                if (key_at_index == key_to_insert) {
                    free(cursor);
                    return EXECUTE_DUPLICATE_KEY;
                }
            }

            leaf_node_insert(cursor, key_to_insert, row_to_insert);

            free(cursor);
            return EXECUTE_SUCCESS;
        }
        case (STATEMENT_SELECT):
            return execute_select(statement, table);
    }
    return EXECUTE_SUCCESS;
}

// Bascially executes the result of execute_select whenever it's detected it gets the raw data from our row then prints our rows
ExecuteResult execute_select(Statement* statement, Table* table) {
    (void)statement;
    // Tells our cursor to start the table
    Cursor* cursor = table_start(table);
    // Creates an row from the struct
    Row row;

    if (cursor == NULL) {
        printf("Error: Cursor is NULL");
        exit(EXIT_FAILURE);
    }
    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }

    free(cursor);
    return EXECUTE_SUCCESS;
}

/* --- Serialization --- */

void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// We Convert Row struct into raw bytes and then write to page memory
// We do this so we can store our Row struct into raw bytes and write it to page memory for later use
void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

// Convert raw bytes from page memory back into a Row struct
void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

/* --- Table Management --- */

Table* db_open(const char* filename) {
    // 1. Open the database file and get a pager
    Pager* pager = pager_open(filename);

    Table* table = malloc(sizeof(Table));
    if (table == NULL) {
        fprintf(stderr, "Error: malloc failed for Table\n");
        exit(EXIT_FAILURE);
    }

    table->pager = pager;
    table->root_page_num = 0;

    if (pager->file_length == 0) {
        void* root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
        set_node_root(root_node, true);
    }

    return table;
}

// This gives the memory back to the computer so other programs can use it when we're done.
void free_table(Table* table) {
    Pager* pager = table->pager;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        if (pager->pages[i] != NULL) {
            // Flush pages that are within the valid range
            if (i < pager->num_pages) {
                pager_flush(pager, i);
            }
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    close(pager->file_descriptor);
    free(pager);
    free(table);
}

/* --- Cursor Management --- */

// Find the memory address where a specific row is stored
// Calculates which page the row is on and the byte offset within that page
void* cursor_value(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* page = get_page(cursor->table->pager, page_num);
    return leaf_node_value(page, cursor->cell_num);
}

// Adds one to our row_num and checks if cursor is to row num and cursor is to table and num_rows then cursor end_of_table = true
void cursor_advance(Cursor* cursor) {
    uint32_t page_num = cursor->page_num;
    void* node = get_page(cursor->table->pager, page_num);

    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
        cursor->end_of_table = true;
    }
}

Cursor* table_start(Table* table) {
    Cursor* cursor = malloc(sizeof(Cursor));
    if (cursor == NULL) {
        fprintf(stderr, "Error: malloc failed for Cursor\n");
        exit(EXIT_FAILURE);
    }

    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;

    void* root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);

    return cursor;
}

/* --- Leaf Node Management --- */

uint32_t* leaf_node_num_cells(void* node) {
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

NodeType get_node_type(void* node) {
    uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSET));
    return (NodeType)value;
}

void set_node_type(void* node, NodeType type) {
    uint8_t value = type;
    *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
}

bool is_node_root(void* node) {
    uint8_t value = *((uint8_t*)(node + IS_ROOT_OFFSET));
    return (bool)value;
}

void set_node_root(void* node, bool is_root) {
    uint8_t value = is_root;
    *((uint8_t*)(node + IS_ROOT_OFFSET)) = value;
}

void initialize_leaf_node(void* node) {
    set_node_type(node, NODE_LEAF);
    set_node_root(node, false);
    *leaf_node_num_cells(node) = 0;
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(cursor, key, value);
        return;
    }

    if (cursor->cell_num < num_cells) {
        // Make room for new cell
        for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }

    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

// Binary search within a leaf node to find the correct position for a key
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = get_page(table->pager, page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    Cursor* cursor = malloc(sizeof(Cursor));
    if (cursor == NULL) {
        fprintf(stderr, "Error: malloc failed for Cursor\n");
        exit(EXIT_FAILURE);
    }

    cursor->table = table;
    cursor->page_num = page_num;

    // Binary search
    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;
    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);
        if (key == key_at_index) {
            cursor->cell_num = index;
            return cursor;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor->cell_num = min_index;
    return cursor;
}

// Uses Binary Search to quickly find the exact "parking spot" (index) for a key inside this node.
uint32_t leaf_node_find_cell(void* node, uint32_t key) {
    uint32_t num_cells = *leaf_node_num_cells(node);

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cells;

    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);
        if (key == key_at_index) {
            return index;
        }
        if (key < key_at_index) {
            one_past_max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    return min_index;
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    /*
    Create a new node and move half the cells over.
    Insert the new value in one of the two nodes.
    Update parent or create a new parent.
    */

    void* old_node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void* new_node = get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);

    /*
    All existing keys plus new key should be divided
    evenly between old (left) and new (right) nodes.
    Starting from the right, move each key to correct position.
    */
    for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
        void* destination_node;
        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
            destination_node = new_node;
        } else {
            destination_node = old_node;
        }
        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void* destination = leaf_node_cell(destination_node, index_within_node);

        if (i == (int32_t)cursor->cell_num) {
            serialize_row(value, destination);
        } else if (i > (int32_t)cursor->cell_num) {
            memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
        } else {
            memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
        }
    }

    /* Update cell count on both leaf nodes */
    *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

    if (is_node_root(old_node)) {
        return create_new_root(cursor->table, new_page_num);
    } else {
        printf("Need to implement updating parent after split\n");
        exit(EXIT_FAILURE);
    }
}

void create_new_root(Table* table, uint32_t right_child_page_num) {
    /*
    Handle splitting the root.
    Old root copied to new page, becomes left child.
    Address of right child passed in.
    Re-initialize root page to contain the new root node.
    New root node points to two children.
    */

    void* root = get_page(table->pager, table->root_page_num);
    void* right_child = get_page(table->pager, right_child_page_num);
    uint32_t left_child_page_num = get_unused_page_num(table->pager);
    void* left_child = get_page(table->pager, left_child_page_num);

    /* Left child has data from old root */
    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    /* Root node is a new internal node with one key and two children */
    set_node_type(root, NODE_INTERNAL);
    set_node_root(root, true);
    /* Internal node implementation needed here, for now stubbed */
    printf("Internal nodes not fully implemented yet.\n");
    (void)right_child;
}

uint32_t get_unused_page_num(Pager* pager) {
    return pager->num_pages;
}

void print_leaf_node(void* node) {
    uint32_t num_cells = *leaf_node_num_cells(node);
    printf("leaf (size %d)\n", num_cells);
    for (uint32_t i = 0; i < num_cells; i++) {
        uint32_t key = *leaf_node_key(node, i);
        printf("   -%d : %d\n", i, key);
    }
}

/* --- Pager (Storage) Implementation --- */

// Open a database file using pager
Pager* pager_open(const char* filename) {
    // This stores our flags like write, read, create, close
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);

    // Checks if file failed to open (fd = -1 means error)
    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    // Creates memory in RAM for Pager
    Pager* pager = malloc(sizeof(Pager));
    if (pager == NULL) {
        fprintf(stderr, "Error: malloc failed for Pager\n");
        exit(EXIT_FAILURE);
    }

    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    // Sets index of pages to NULL
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    return pager;
}

// This finds the data in RAM first. If it's not there, it goes to the (Hard Drive) to fetch it.
void* get_page(Pager* pager, uint32_t page_num) {
    // Checks if TABLE_MAX_PAGES is greater than page_num if yes then return EXIT_FAILURE
    if (page_num >= TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds. %d >= %d\n", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    // Checks if pages struct and inside that page_num and if it equals to NULL if not then we just return it
    if (pager->pages[page_num] == NULL) {
        // Cache miss. Allocate memory and load from file.
        void* page = malloc(PAGE_SIZE);
        if (page == NULL) {
            fprintf(stderr, "Error: malloc failed for page\n");
            exit(EXIT_FAILURE);
        }

        uint32_t num_pages = pager->file_length / PAGE_SIZE;

        // We might save a partial page at the end of the file
        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num < num_pages) {
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }

    return pager->pages[page_num];
}

// Writes data from RAM to the hard drive
void pager_flush(Pager* pager, uint32_t page_num) {
    // Checks if it equals to NULL pointer which stands for nothing in the pointer if so we exit the program
    if (pager->pages[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

/* --- Application Entry Point --- */

int main(int argc, char* argv[]) {
    // Checks if you gave a database filename if not then exit
    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);

    InputBuffer* input_buffer = new_input_buffer();
    Statement statement;
    while (true) {
        print_prompt();
        read_input(input_buffer);
        // Checks if the first character in the input_buffer is . then execute do_meta_command
        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_COMMAND_UNRECOGNIZED_COMMAND):
                    printf("Unrecognized command: '%s'.\n", input_buffer->buffer);
                    continue;
            }
        }

        switch (prepare_statement(input_buffer, &statement)) {
            case (PREPARE_SUCCESS):
                break;
            case (PREPARE_STRING_TOO_LONG):
                printf("String is too long.\n");
                continue;
            case (PREPARE_SYNTAX_ERROR):
                printf("Syntax error. Could not parse statement.\n");
                continue;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
                continue;
            case (PREPARE_NEGATIVE_ID):
                printf("ID must be positive.\n");
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case (EXECUTE_SUCCESS):
                printf("Executed.\n");
                break;
            case (EXECUTE_TABLE_FULL):
                printf("Error: Table full.\n");
                break;
            case (EXECUTE_DUPLICATE_KEY):
                printf("Error: Duplicate key.\n");
                break;
        }
    }
}
