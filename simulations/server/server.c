#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_ITEMS 100

typedef struct {
    int id;
    char name[64];
    double price;
    int stock;
} GroceryItem;

// In-memory database
GroceryItem inventory[MAX_ITEMS] = {
    {1, "Apples (1kg)", 2.99, 50},
    {2, "Whole Milk (1L)", 1.49, 30},
    {3, "Sourdough Bread", 3.50, 20},
    {4, "Free-Range Eggs (12)", 4.25, 25},
    {5, "Basmati Rice (2kg)", 5.99, 40}
};
int item_count = 5;
int next_id = 6;
int server_fd;

// Clean exit on Ctrl+C
void handle_sigint(int sig) {
    (void)sig;
    printf("\nShutting down grocery server...\n");
    close(server_fd);
    exit(0);
}

// Helper to send HTTP responses
void send_response(int client_fd, const char *status, const char *content_type, const char *body) {
    char header[512];
    int body_len = (int)strlen(body);
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n\r\n",
             status, content_type, body_len);

    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, body_len, 0);
}

// Simple URL decoder for form data (replaces '+' with ' ')
void url_decode(char *dst, const char *src) {
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int val;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Extract key=value from application/x-www-form-urlencoded body
int get_form_param(const char *body, const char *key, char *out_val, size_t max_len) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    const char *start = strstr(body, search_key);
    if (!start) return 0;

    start += strlen(search_key);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= max_len) len = max_len - 1;

    char encoded[256];
    strncpy(encoded, start, len);
    encoded[len] = '\0';
    url_decode(out_val, encoded);
    return 1;
}

// Route: GET /items
void handle_get_items(int client_fd) {
    char json[BUFFER_SIZE];
    int offset = snprintf(json, sizeof(json), "[\n");

    for (int i = 0; i < item_count; i++) {
        offset += snprintf(json + offset, sizeof(json) - offset,
                           "  {\"id\": %d, \"name\": \"%s\", \"price\": %.2f, \"stock\": %d}%s\n",
                           inventory[i].id, inventory[i].name, inventory[i].price, inventory[i].stock,
                           (i < item_count - 1) ? "," : "");
    }
    snprintf(json + offset, sizeof(json) - offset, "]");
    send_response(client_fd, "200 OK", "application/json", json);
}

// Route: POST /items (body: name=Banana&price=1.20&stock=100)
void handle_add_item(int client_fd, const char *body) {
    if (item_count >= MAX_ITEMS) {
        send_response(client_fd, "507 Insufficient Storage", "application/json", "{\"error\": \"Inventory full\"}");
        return;
    }

    char name[64], price_str[32], stock_str[32];
    if (!get_form_param(body, "name", name, sizeof(name)) ||
        !get_form_param(body, "price", price_str, sizeof(price_str)) ||
        !get_form_param(body, "stock", stock_str, sizeof(stock_str))) {
        send_response(client_fd, "400 Bad Request", "application/json",
                      "{\"error\": \"Missing name, price, or stock\"}");
        return;
    }

    GroceryItem *item = &inventory[item_count++];
    item->id = next_id++;
    strncpy(item->name, name, sizeof(item->name) - 1);
    item->name[sizeof(item->name) - 1] = '\0';
    item->price = atof(price_str);
    item->stock = atoi(stock_str);

    char res[256];
    snprintf(res, sizeof(res), "{\"message\": \"Item added\", \"id\": %d}", item->id);
    send_response(client_fd, "201 Created", "application/json", res);
}

// Route: POST /buy (body: id=1&qty=2)
void handle_buy_item(int client_fd, const char *body) {
    char id_str[32], qty_str[32];
    if (!get_form_param(body, "id", id_str, sizeof(id_str)) ||
        !get_form_param(body, "qty", qty_str, sizeof(qty_str))) {
        send_response(client_fd, "400 Bad Request", "application/json", "{\"error\": \"Missing id or qty\"}");
        return;
    }

    int id = atoi(id_str);
    int qty = atoi(qty_str);
    if (qty <= 0) {
        send_response(client_fd, "400 Bad Request", "application/json", "{\"error\": \"Invalid quantity\"}");
        return;
    }

    for (int i = 0; i < item_count; i++) {
        if (inventory[i].id == id) {
            if (inventory[i].stock < qty) {
                send_response(client_fd, "409 Conflict", "application/json", "{\"error\": \"Insufficient stock\"}");
                return;
            }
            inventory[i].stock -= qty;
            double total = inventory[i].price * qty;

            char res[256];
            snprintf(res, sizeof(res),
                     "{\"message\": \"Purchase successful\", \"item\": \"%s\", \"qty\": %d, \"total\": %.2f, \"remaining_stock\": %d}",
                     inventory[i].name, qty, total, inventory[i].stock);
            send_response(client_fd, "200 OK", "application/json", res);
            return;
        }
    }

    send_response(client_fd, "404 Not Found", "application/json", "{\"error\": \"Item not found\"}");
}

// Route: GET / (Simple HTML Storefront)
void handle_home(int client_fd) {
    const char *html =
        "<!DOCTYPE html><html><head><title>C Grocery Store</title>"
        "<style>body{font-family:sans-serif;max-width:600px;margin:40px auto;}"
        "table{width:100%;border-collapse:collapse;}th,td{padding:8px;border:1px solid #ddd;text-align:left;}</style></head>"
        "<body><h1>C Grocery Store API</h1>"
        "<p>Available API Endpoints:</p>"
        "<ul>"
        "<li><b>GET /items</b> - View inventory JSON</li>"
        "<li><b>POST /items</b> - Add item (form: <code>name=...&price=...&stock=...</code>)</li>"
        "<li><b>POST /buy</b> - Purchase item (form: <code>id=...&qty=...</code>)</li>"
        "</ul>"
        "<h3>Live Inventory</h3><table><thead><tr><th>ID</th><th>Item</th><th>Price</th><th>Stock</th></tr></thead>"
        "<tbody id='list'></tbody></table>"
        "<script>fetch('/items').then(r=>r.json()).then(d=>{"
        "document.getElementById('list').innerHTML=d.map(i=>`<tr><td>${i.id}</td><td>${i.name}</td><td>$${i.price.toFixed(2)}</td><td>${i.stock}</td></tr>`).join('');"
        "});</script></body></html>";
    send_response(client_fd, "200 OK", "text/html", html);
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';

    // Parse Method and Path
    char method[16], path[128];
    if (sscanf(buffer, "%15s %127s", method, path) != 2) {
        close(client_fd);
        return;
    }

    // Locate HTTP body (after \r\n\r\n)
    const char *body = strstr(buffer, "\r\n\r\n");
    body = body ? (body + 4) : "";

    printf("[%s] %s\n", method, path);

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        handle_home(client_fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/items") == 0) {
        handle_get_items(client_fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/items") == 0) {
        handle_add_item(client_fd, body);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/buy") == 0) {
        handle_buy_item(client_fd, body);
    } else {
        send_response(client_fd, "404 Not Found", "application/json", "{\"error\": \"Endpoint not found\"}");
    }

    close(client_fd);
}

int main() {
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    signal(SIGINT, handle_sigint);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Grocery Server running on http://localhost:%d\n", PORT);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }
        handle_client(client_fd);
    }

    return 0;
}
