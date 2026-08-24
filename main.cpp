#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>

#include <curl/curl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace fs = std::filesystem;


// =====================================================
// HEALTH SERVER FOR RENDER
// =====================================================

void startHealthServer()
{
    const char* portEnv = std::getenv("PORT");

    int port = 10000;

    if (portEnv)
    {
        try
        {
            port = std::stoi(portEnv);
        }
        catch (...)
        {
            port = 10000;
        }
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        std::cerr << "Failed to create HTTP socket." << std::endl;
        return;
    }

    int opt = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (
        bind(
            server_fd,
            (struct sockaddr*)&address,
            sizeof(address)
        ) < 0
    )
    {
        std::cerr << "Failed to bind HTTP server." << std::endl;

        close(server_fd);

        return;
    }

    if (listen(server_fd, 10) < 0)
    {
        std::cerr << "Failed to listen on HTTP server." << std::endl;

        close(server_fd);

        return;
    }

    std::cout
        << "Health server listening on port "
        << port
        << std::endl;

    while (true)
    {
        int client =
            accept(
                server_fd,
                nullptr,
                nullptr
            );

        if (client < 0)
        {
            continue;
        }

        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "OK";

        send(
            client,
            response,
            117,
            0
        );

        close(client);
    }
}


// =====================================================
// CURL WRITE CALLBACK
// =====================================================

size_t WriteCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    std::string* output
)
{
    size_t totalSize = size * nmemb;

    output->append(
        (char*)contents,
        totalSize
    );

    return totalSize;
}


// =====================================================
// SEND HTTP REQUEST
// =====================================================

std::string sendRequest(
    CURL* curl,
    const std::string& url
)
{
    std::string response;

    curl_easy_reset(curl);

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

    CURLcode result =
        curl_easy_perform(curl);

    if (result != CURLE_OK)
    {
        std::cout
            << "CURL Error: "
            << curl_easy_strerror(result)
            << std::endl;
    }

    return response;
}


// =====================================================
// GET VALUE FROM JSON
// =====================================================

std::string getValue(
    const std::string& json,
    const std::string& key
)
{
    size_t pos =
        json.find(key);

    if (pos == std::string::npos)
        return "";

    pos += key.length();

    size_t end =
        json.find_first_of(",}", pos);

    if (end == std::string::npos)
        return "";

    return json.substr(
        pos,
        end - pos
    );
}


// =====================================================
// GET MESSAGE TEXT
// =====================================================

std::string getText(
    const std::string& json
)
{
    std::string key =
        "\"text\":\"";

    size_t pos =
        json.find(key);

    if (pos == std::string::npos)
        return "";

    pos += key.length();

    size_t end =
        json.find("\"", pos);

    if (end == std::string::npos)
        return "";

    return json.substr(
        pos,
        end - pos
    );
}


// =====================================================
// SEND TELEGRAM MESSAGE
// =====================================================

void sendMessage(
    CURL* curl,
    const std::string& botToken,
    const std::string& chatID,
    const std::string& message
)
{
    std::string url =
        "https://api.telegram.org/bot" +
        botToken +
        "/sendMessage?chat_id=" +
        chatID +
        "&text=" +
        message;

    sendRequest(
        curl,
        url
    );
}


// =====================================================
// SEND VIDEO TO TELEGRAM
// =====================================================

bool sendVideo(
    CURL* curl,
    const std::string& botToken,
    const std::string& chatID,
    const std::string& fileName
)
{
    std::string url =
        "https://api.telegram.org/bot" +
        botToken +
        "/sendVideo";

    curl_easy_reset(curl);

    curl_mime* mime =
        curl_mime_init(curl);

    if (!mime)
    {
        return false;
    }

    // chat_id

    curl_mimepart* part =
        curl_mime_addpart(mime);

    curl_mime_name(
        part,
        "chat_id"
    );

    curl_mime_data(
        part,
        chatID.c_str(),
        CURL_ZERO_TERMINATED
    );


    // video

    part =
        curl_mime_addpart(mime);

    curl_mime_name(
        part,
        "video"
    );

    curl_mime_filedata(
        part,
        fileName.c_str()
    );


    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_MIMEPOST,
        mime
    );


    std::string response;

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );


    CURLcode result =
        curl_easy_perform(curl);


    curl_mime_free(mime);


    if (result != CURLE_OK)
    {
        std::cout
            << "Upload Error: "
            << curl_easy_strerror(result)
            << std::endl;

        return false;
    }


    std::cout
        << "Telegram response: "
        << response
        << std::endl;

    return true;
}


// =====================================================
// RUN YT-DLP - LINUX VERSION
// =====================================================

bool downloadVideo(
    const std::string& videoURL,
    const std::string& outputPath
)
{
    std::cout
        << "Starting yt-dlp..."
        << std::endl;

    std::string commandLine =
        "yt-dlp "
        "--no-playlist "
        "-f \"best[ext=mp4]/best\" "
        "-o \"" +
        outputPath +
        "\" \"" +
        videoURL +
        "\"";


    std::cout
        << "Running yt-dlp..."
        << std::endl;


    int result =
        std::system(
            commandLine.c_str()
        );


    if (result == 0)
    {
        std::cout
            << "yt-dlp finished successfully."
            << std::endl;

        return true;
    }


    std::cout
        << "yt-dlp failed."
        << std::endl;

    std::cout
        << "Exit Code: "
        << result
        << std::endl;

    return false;
}


// =====================================================
// MAIN
// =====================================================

int main()
{
    // =================================================
    // START RENDER HEALTH SERVER
    // =================================================

    std::thread healthServer(
        startHealthServer
    );

    healthServer.detach();


    // =================================================
    // GET BOT TOKEN FROM ENVIRONMENT VARIABLE
    // =================================================

    const char* token =
        std::getenv("BOT_TOKEN");


    if (!token)
    {
        std::cerr
            << "BOT_TOKEN is not set!"
            << std::endl;

        return 1;
    }


    std::string botToken =
        token;


    // =================================================
    // INITIALIZE CURL
    // =================================================

    CURL* curl =
        curl_easy_init();


    if (!curl)
    {
        std::cout
            << "Failed to initialize CURL!"
            << std::endl;

        return 1;
    }


    std::cout
        << "Bot is running..."
        << std::endl;


    long long offset = 0;


    // =================================================
    // BOT LOOP
    // =================================================

    while (true)
    {
        std::string url =
            "https://api.telegram.org/bot" +
            botToken +
            "/getUpdates?timeout=30&offset=" +
            std::to_string(offset);


        std::string response =
            sendRequest(
                curl,
                url
            );


        // =================================================
        // CHECK NEW MESSAGE
        // =================================================

        if (
            response.find(
                "\"result\":[{"
            ) != std::string::npos
        )
        {
            // =================================================
            // UPDATE ID
            // =================================================

            std::string updateID =
                getValue(
                    response,
                    "\"update_id\":"
                );


            if (!updateID.empty())
            {
                offset =
                    std::stoll(
                        updateID
                    ) + 1;
            }


            // =================================================
            // CHAT ID
            // =================================================

            std::string chatID =
                getValue(
                    response,
                    "\"chat\":{\"id\":"
                );


            // =================================================
            // MESSAGE TEXT
            // =================================================

            std::string text =
                getText(
                    response
                );


            std::cout
                << "Received: "
                << text
                << std::endl;


            // =================================================
            // START COMMAND
            // =================================================

            if (text == "/start")
            {
                sendMessage(
                    curl,
                    botToken,
                    chatID,
                    "Welcome!%20Send%20me%20a%20Facebook%20video%20link."
                );


                std::cout
                    << "Start message sent."
                    << std::endl;
            }


            // =================================================
            // FACEBOOK LINK
            // =================================================

            else if (
                text.find(
                    "facebook.com"
                ) != std::string::npos
                ||
                text.find(
                    "fb.watch"
                ) != std::string::npos
            )
            {
                std::cout
                    << "Facebook link received!"
                    << std::endl;


                sendMessage(
                    curl,
                    botToken,
                    chatID,
                    "Downloading%20your%20video..."
                );


                // =================================================
                // LINUX TEMPORARY VIDEO PATH
                // =================================================

                std::string videoPath =
                    "/tmp/facebook_video.mp4";


                if (
                    fs::exists(
                        videoPath
                    )
                )
                {
                    fs::remove(
                        videoPath
                    );
                }


                // =================================================
                // DOWNLOAD
                // =================================================

                std::cout
                    << "Downloading..."
                    << std::endl;


                bool downloaded =
                    downloadVideo(
                        text,
                        videoPath
                    );


                // =================================================
                // DOWNLOAD SUCCESS
                // =================================================

                if (
                    downloaded
                    &&
                    fs::exists(
                        videoPath
                    )
                )
                {
                    std::cout
                        << "Download successful!"
                        << std::endl;


                    sendMessage(
                        curl,
                        botToken,
                        chatID,
                        "Uploading%20video..."
                    );


                    // =================================================
                    // SEND VIDEO
                    // =================================================

                    bool uploaded =
                        sendVideo(
                            curl,
                            botToken,
                            chatID,
                            videoPath
                        );


                    if (uploaded)
                    {
                        std::cout
                            << "Video sent successfully!"
                            << std::endl;


                        sendMessage(
                            curl,
                            botToken,
                            chatID,
                            "Done!"
                        );
                    }
                    else
                    {
                        std::cout
                            << "Failed to send video."
                            << std::endl;


                        sendMessage(
                            curl,
                            botToken,
                            chatID,
                            "Failed%20to%20send%20the%20video."
                        );
                    }


                    // =================================================
                    // DELETE DOWNLOADED VIDEO
                    // =================================================

                    if (
                        fs::exists(
                            videoPath
                        )
                    )
                    {
                        fs::remove(
                            videoPath
                        );
                    }
                }


                // =================================================
                // DOWNLOAD FAILED
                // =================================================

                else
                {
                    std::cout
                        << "Download failed!"
                        << std::endl;


                    sendMessage(
                        curl,
                        botToken,
                        chatID,
                        "Sorry,%20I%20could%20not%20download%20this%20video."
                    );
                }
            }
        }


        // =================================================
        // SMALL DELAY
        // =================================================

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500)
        );
    }


    curl_easy_cleanup(curl);

    return 0;
}
