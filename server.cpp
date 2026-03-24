#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <cstdlib>
#include <iterator>

// define a structure to hold appointment data.
// This allows us to group related strings into a single object.
struct Appointment 
{
    std::string name;
    std::string date;
    std::string time;
    std::string description;
    std::string image;
};

// Global database vector to hold appointments while the server is running.
std::vector<Appointment> database;
// Global search string used to filter the dashboard view.
std::string currentSearch = "";

// This function handles URL decoding. Browsers convert spaces to %20.
// decode them so the server can find the actual files on the disk.
std::string urlDecode(std::string text) 
{
    std::string out;
    for (size_t i = 0; i < text.length(); i++) 
    {
        if (text[i] == '%' && i + 2 < text.length()) 
        {
            int value = strtol(text.substr(i + 1, 2).c_str(), NULL, 16);
            out += static_cast<char>(value);
            i += 2;
        } else if (text[i] == '+') 
        {
            out += ' ';
        } else 
        {
            out += text[i];
        }
    }
    return out;
}

// Persistence logic: saves the vector to a text file.
// This ensures that the data survives if the server is stopped or restarted.
void saveToDisk() {
    std::ofstream file("appointments.txt");
    for (size_t i = 0; i < database.size(); ++i) 
    {
        file << database[i].name << "|" << database[i].date << "|" << database[i].time << "|" << database[i].description << "|" << database[i].image << "\n";
    }
    file.close();
}

// Loads existing data from the text file into the vector on startup.
void loadFromDisk() 
{
    database.clear();
    std::ifstream file("appointments.txt");
    std::string line;
    while (std::getline(file, line)) 
    {
        std::stringstream ss(line);
        Appointment app;
        std::getline(ss, app.name, '|');
        std::getline(ss, app.date, '|');
        std::getline(ss, app.time, '|');
        std::getline(ss, app.description, '|');
        std::getline(ss, app.image, '|');
        if (!app.name.empty()) database.push_back(app);
    }
}

// Function prototype for the server-side HTML generation.
std::string generateHTML();

// Parses multipart/form-data POST bodies to extract text fields and file data.
// manually search for boundaries because external libraries are forbidden.
std::string extractField(const std::string& body, const std::string& name, const std::string& boundary) 
{
    std::string search = "name=\"" + name + "\"";
    size_t pos = body.find(search);
    if (pos == std::string::npos) return "";
    size_t start = body.find("\r\n\r\n", pos) + 4;
    size_t end = body.find("\r\n" + boundary, start);
    if (end == std::string::npos) return "";
    return body.substr(start, end - start);
}

int main() 
{
    loadFromDisk();
    
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int port = 8080;

    // TCP Socket setup: Create, Bind to all interfaces, and Listen.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    std::cout << "Server live on port " << port << ":" << std::endl;

    while (true) 
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        char header_buffer[8192] = {0};
        int valread = read(new_socket, header_buffer, 8192);
        if (valread <= 0) { close(new_socket); continue; }
        
        std::string request(header_buffer, valread);
        size_t path_start = request.find(" ") + 1;
        size_t path_end = request.find(" ", path_start);
        if (path_start == 0 || path_end == std::string::npos) { close(new_socket); continue; }
        std::string full_url = request.substr(path_start, path_end - path_start);

        // ROUTE: Image Viewing. 
        // read binary data and stream it with a Content-Type header.
        // close the socket immediately after sending to stop the browser loading spinner.
        if (full_url.find("/image/") == 0) 
        {
            std::string filename = urlDecode(full_url.substr(7));
            std::ifstream file(filename.c_str(), std::ios::binary);
            if (file) 
            {
                std::string img_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::stringstream ss; ss << img_data.length();
                
                std::string header = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n";
                header += "Content-Length: " + ss.str() + "\r\nConnection: close\r\n\r\n";
                
                send(new_socket, header.c_str(), header.length(), 0);
                send(new_socket, img_data.c_str(), img_data.length(), 0);
            } 
            else 
            {
                std::string nf = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
                send(new_socket, nf.c_str(), nf.length(), 0);
            }
            close(new_socket);
            continue;
        }

        // ROUTE: Search. 
        // Updates the global search filter and redirects back to the root page.
        if (full_url.find("/search?") == 0) 
        {
            size_t q_pos = full_url.find("q=");
            currentSearch = (q_pos != std::string::npos) ? urlDecode(full_url.substr(q_pos + 2)) : "";
            
            std::string res = "HTTP/1.1 303 See Other\r\nLocation: /\r\nConnection: close\r\n\r\n";
            send(new_socket, res.c_str(), res.length(), 0);
            close(new_socket);
            continue;
        }

        // ROUTE: Delete. 
        // Removes the record by index and updates the persistent text file.
        if (full_url.find("/delete?") == 0) 
        {
            size_t id_pos = full_url.find("id=");
            if (id_pos != std::string::npos) 
            {
                int id = atoi(full_url.substr(id_pos + 3).c_str());
                if (id >= 0 && id < (int)database.size()) database.erase(database.begin() + id);
                saveToDisk();
            }
            std::string res = "HTTP/1.1 303 See Other\r\nLocation: /\r\nConnection: close\r\n\r\n";
            send(new_socket, res.c_str(), res.length(), 0);
            close(new_socket);
            continue;
        }

        // ROUTE: POST Insertion. 
        // Handles the multipart file and text fields to create a new entry.
        if (request.find("POST /add") == 0)
        {
            size_t cl_pos = request.find("Content-Length: ");
            int content_length = atoi(request.substr(cl_pos + 16, request.find("\r\n", cl_pos) - (cl_pos + 16)).c_str());
            size_t ct_pos = request.find("boundary=");
            std::string boundary = "--" + request.substr(ct_pos + 9, request.find("\r\n", ct_pos) - (ct_pos + 9));
            
            size_t body_start = request.find("\r\n\r\n") + 4;
            std::string body = request.substr(body_start);
            int remaining = content_length - body.length();
            if (remaining > 0)
            {
                std::vector<char> buffer(remaining);
                int total_read = 0;
                while (total_read < remaining) 
                {
                    int bytes = read(new_socket, buffer.data() + total_read, remaining - total_read);
                    if (bytes <= 0) break;
                    total_read += bytes;
                }
                body.append(buffer.data(), total_read);
            }

            Appointment a;
            a.name = extractField(body, "name", boundary);
            a.date = extractField(body, "date", boundary);
            a.time = extractField(body, "time", boundary);
            a.description = extractField(body, "description", boundary);

            size_t f_pos = body.find("filename=\"");
            if (f_pos != std::string::npos) 
            {
                size_t f_end = body.find("\"", f_pos + 10);
                a.image = body.substr(f_pos + 10, f_end - (f_pos + 10));
                if (!a.image.empty()) 
                {
                    size_t d_start = body.find("\r\n\r\n", f_end) + 4;
                    size_t d_end = body.find("\r\n" + boundary, d_start);
                    std::ofstream out(a.image.c_str(), std::ios::binary);
                    out.write(body.c_str() + d_start, d_end - d_start);
                    out.close();
                }
            }
            if (!a.name.empty()) { database.push_back(a); saveToDisk(); }

            std::string res = "HTTP/1.1 303 See Other\r\nLocation: /\r\nConnection: close\r\n\r\n";
            send(new_socket, res.c_str(), res.length(), 0);
            close(new_socket);
            continue;
        }

        // ROUTE: Root.
        // Serves the HTML Dashboard. provide Content-Length so the browser
        // knows exactly how much data to expect before closing the connection.
        std::string html = generateHTML();
        std::stringstream ss; ss << html.length();
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
        response += "Content-Length: " + ss.str() + "\r\nConnection: close\r\n\r\n" + html;
        
        send(new_socket, response.c_str(), response.length(), 0);
        close(new_socket);
    }
    return 0;
}

std::string generateHTML() 
{
    // CSS for html
    std::string html = "<!DOCTYPE html><html><head><style>"
    "body{font-family:sans-serif; background:#fff; padding:20px; color:#000;}"
    ".container{max-width:800px; margin:auto;}"
    ".section{border:1px solid #ccc; padding:20px; margin-bottom:20px;}"
    "input,textarea{width:100%; padding:5px; margin-bottom:10px; border:1px solid #777;}"
    "button{padding:10px; cursor:pointer; background:#eee; border:1px solid #777; font-weight:bold;}"
    "table{width:100%; border-collapse:collapse;} th,td{padding:10px; border:1px solid #ccc; text-align:left;}"
    ".img-circle{width:60px; height:60px; border-radius:50%; object-fit:cover;}"
    "</style></head><body><div class='container'>"
    
    "<div class='section'><h3>Search Contacts</h3>"
    "<form method='GET' action='/search' style='display:flex; gap:10px;'>"
    "<input type='text' name='q' placeholder='Name...' style='margin:0;'>"
    "<button type='submit'>Search</button>"
    "<button type='button' onclick=\"window.location.href='/search?q='\">Clear</button></form></div>"

    "<div class='section'><h3>Schedule Appointment</h3>"
    "<form method='POST' action='/add' enctype='multipart/form-data'>"
    "Name: <input type='text' name='name' required><br>"
    "Date: <input type='date' name='date' required> "
    "Time: <input type='time' name='time' required><br>"
    "Notes: <textarea name='description'></textarea><br>"
    "Picture: <input type='file' name='image' accept='image/*'><br>"
    "<button type='submit'>Save Appointment</button></form></div>"

    "<div class='section'><h3>Appointments Dashboard</h3>"
    "<table><tr><th>Pic</th><th>Details</th><th>Scheduled</th><th>Action</th></tr>";

    for (size_t i = 0; i < database.size(); ++i) 
    {
        // Filtering logic based on search input.
        if (!currentSearch.empty() && database[i].name.find(currentSearch) == std::string::npos) continue;

        std::stringstream ss; ss << i; std::string id = ss.str();
        html += "<tr><td>";
        if (!database[i].image.empty()) 
        {
            html += "<img src='/image/" + database[i].image + "' class='img-circle'>";
        } 
        else 
        {
            html += "None";
        }
        html += "</td><td><strong>" + database[i].name + "</strong><br>" + database[i].description + "</td>";
        html += "<td>" + database[i].date + " at " + database[i].time + "</td>";
        html += "<td><a href='/delete?id=" + id + "'>Delete</a></td></tr>";
    }
    html += "</table></div></div></body></html>";
    return html;
}