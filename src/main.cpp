#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <random>
#include <thread>
#include <chrono>
#include <unordered_map>

const int CHIP8_WIDTH = 64;
const int CHIP8_HEIGHT = 32;

bool setPixel(uint8_t* display, int x, int y) {
    x %= CHIP8_WIDTH;  // wrap horizontally
    y %= CHIP8_HEIGHT; // wrap vertically
    int index = y * CHIP8_WIDTH + x;
    bool erased = display[index] == 1;
    display[index] ^= 1; // toggle pixel
    return erased;
}

int main(int argc, char* argv[]) {
    unsigned char memory[4096] = {0};
    uint8_t display[CHIP8_WIDTH * CHIP8_HEIGHT] = {0};

    // Registers
    uint8_t V[16];

    uint16_t I; // Index Register (Points to memory address in Ram)
    uint16_t PC; // Program Counter (points to current instruction in memory)
    unsigned short stack[16];
    unsigned short sp = 0; // points to next free slot
    uint8_t DT = 0;
    uint8_t ST = 0;

    // Open ROM
    std::ifstream rom("Pong.ch8", std::ios::binary | std::ios::ate);
    if (!rom) {
        std::cerr << "Failed to open ROM\n";
        return 1;
    }

    // Get size
    std::streamsize size = rom.tellg();
    rom.seekg(0, std::ios::beg);

    // Read ROM into buffer
    std::vector<char> buffer(size);
    if (!rom.read(buffer.data(), size)) {
        std::cerr << "Failed to read ROM\n";
        return 1;
    }

    // Load into memory at 0x200
    for (int i = 0; i < size; i++) {
        memory[0x200 + i] = buffer[i];
    }

    // std::cout << "ROM size: " << size << " bytes\n";
    // for (size_t i = 0; i < std::min<size_t>(size, 64); ++i) {
    //     std::cout << std::hex << std::setfill('0')
    //             << std::setw(3) << (0x200 + i) << ": "
    //             << std::setw(2) << (static_cast<int>(memory[0x200 + i]) & 0xFF)
    //             << "\n";
    // }
    // std::cout << std::dec;

    PC = 0x200;

    uint8_t chip8_fontset[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    for (int i = 0; i < 80; i++) {
        memory[0x50 + i] = chip8_fontset[i];
    }

    std::unordered_map<SDL_Keycode, uint8_t> keymap = {
        {SDLK_x, 0x0}, {SDLK_1, 0x1}, {SDLK_2, 0x2}, {SDLK_3, 0x3},
        {SDLK_q, 0x4}, {SDLK_w, 0x5}, {SDLK_e, 0x6}, {SDLK_a, 0x7},
        {SDLK_s, 0x8}, {SDLK_d, 0x9}, {SDLK_z, 0xA}, {SDLK_c, 0xB},
        {SDLK_4, 0xC}, {SDLK_r, 0xD}, {SDLK_f, 0xE}, {SDLK_v, 0xF}
    };

    bool keys[16] = {false};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distrib(0, 255);
    size_t program_size = sizeof(memory - 512);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CHIP-8 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        CHIP8_WIDTH * 10,
        CHIP8_HEIGHT * 10,
        SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const int CPU_HZ = 500; // instructions per second
    const int TIMER_HZ = 60; // timers per second

    auto lastCpu = std::chrono::high_resolution_clock::now();
    auto lastTimer = std::chrono::high_resolution_clock::now();

    auto lastTimerUpdate = std::chrono::high_resolution_clock::now();
    bool quit = false;
    SDL_Event event;
    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) quit = true;

            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                bool pressed = (event.type == SDL_KEYDOWN);
                if (keymap.find(event.key.keysym.sym) != keymap.end()) {
                    uint8_t chip8Key = keymap[event.key.keysym.sym];
                    keys[chip8Key] = pressed;
                }
            }
        }

        

        auto now = std::chrono::high_resolution_clock::now();
        

        // CPU step
        if (std::chrono::duration_cast<std::chrono::microseconds>(now - lastCpu).count() >= 1000000 / CPU_HZ) {
            const int CYCLES_PER_FRAME = 10;
            for (int i = 0; i < CYCLES_PER_FRAME; i++) {
                uint16_t instruction = (memory[PC] << 8) | memory[PC + 1];
                std::cout << "Current Instruction: " << std::hex << instruction << std::endl;
                
                PC += 2;

                switch (instruction & 0xF000) {
                case 0x0000:
                    switch (instruction & 0x00FF) {
                        case 0x00E0:
                            for (int i = 0; i < CHIP8_WIDTH * CHIP8_HEIGHT; i++) {
                                display[i] = 0;
                            }
                            break;
                        case 0x00EE:
                            sp--;
                            PC = stack[sp];
                            break;
                    }
                    break;
                case 0x1000:
                    PC = instruction & 0x0FFF;
                    break;
                case 0x2000:
                    stack[sp] = PC;
                    sp++;
                    PC = instruction & 0x0FFF;
                    break;
                case 0x3000:
                    if (V[(instruction & 0x0F00) >> 8] == V[instruction & 0x00FF]) {
                        PC += 2;
                    }
                    break;
                case 0x4000:
                    if (V[(instruction & 0x0F00) >> 8] != V[instruction & 0x00FF]) {
                        PC += 2;
                    }
                    break;
                case 0x5000:
                    if (V[(instruction & 0x0F00) >> 8] == V[(instruction & 0x00F0) >> 4]) {
                        PC += 2;
                    }
                    break;
                case 0x6000: {
                    uint8_t x = (instruction & 0x0F00) >> 8;
                    V[x] = instruction & 0x00FF;
                    break;
                }
                case 0x7000:
                    V[(instruction & 0x0F00) >> 8] += instruction & 0x00FF;
                    break;
                case 0x8000:
                    switch (instruction & 0x000F) {
                        case 0x0000: 
                            V[(instruction & 0x0F00) >> 8] = V[(instruction & 0x00F0) >> 4]; 
                            break;
                        case 0x0001: 
                            V[(instruction & 0x0F00) >> 8] = V[(instruction & 0x0F00) >> 8] | V[(instruction & 0x00F0) >> 4]; 
                            break;
                        case 0x0002:
                            V[(instruction & 0x0F00) >> 8] = V[(instruction & 0x0F00) >> 8] & V[(instruction & 0x00F0) >> 4];
                            break;
                        case 0x0003:
                            V[(instruction & 0x0F00) >> 8] = V[(instruction & 0x0F00) >> 8] ^ V[(instruction & 0x00F0) >> 4];
                            break;
                        case 0x0004: {
                            uint8_t x = (instruction & 0x0F00) >> 8;
                            uint8_t y = (instruction & 0x00F0) >> 4;
                            uint16_t sum = V[x] + V[y];
                            V[0xF] = (sum > 255) ? 1 : 0;
                            V[x] = sum & 0xFF;
                            break;
                        }
                        case 0x0005:
                            if (V[(instruction & 0x0F00) >> 8] > V[(instruction & 0x00F0) >> 4]) {
                                V[0xF] = 1;
                            } else {
                                V[0xF] = 0;
                            }

                            V[(instruction & 0x0F00) >> 8] = V[(instruction & 0x0F00) >> 8] - V[(instruction & 0x00F0) >> 4];
                            break;
                        case 0x0006: {
                            uint8_t x = (instruction & 0x0F00) >> 8;

                            // Save LSB into VF
                            V[0xF] = V[x] & 0x1;

                            // Shift Vx right by 1
                            V[x] >>= 1;
                            break;
                        }
                        case 0x0007: {
                            uint8_t x = (instruction & 0x0F00) >> 8;
                            uint8_t y = (instruction & 0x00F0) >> 4;

                            V[0xF] = (V[y] > V[x]) ? 1 : 0;
                            V[x] = V[y] - V[x];
                            break;
                        }
                        case 0x000E: {
                            uint8_t x = (instruction & 0x0F00) >> 8;

                            // Save MSB into VF
                            V[0xF] = (V[x] & 0x80) >> 7;

                            // Shift Vx left by 1
                            V[x] <<= 1;
                            break;
                        }
                    }
                    break;
                case 0x9000:
                    if (V[(instruction & 0x0F00) >> 8] != V[(instruction & 0x00F0) >> 4]) {
                        PC += 2;
                    }
                    break;
                case 0xA000:
                    I = instruction & 0x0FFF;
                    break;
                case 0xB000:
                    PC = (instruction & 0x0FFF) + V[0];
                    break;
                case 0xC000: {
                    uint8_t rand = static_cast<uint8_t>(distrib(gen));
                    V[(instruction & 0x0F00) >> 8] = (instruction & 0x0FF) & rand;
                    break;
                }
                case 0xD000: {
                    uint8_t x = V[(instruction & 0x0F00) >> 8];
                    uint8_t y = V[(instruction & 0x00F0) >> 4];
                    uint8_t height = instruction & 0x000F;

                    V[0xF] = 0; // reset collision flag
                    for (int row = 0; row < height; row++) {
                        uint8_t spriteByte = memory[I + row];
                        for (int col = 0; col < 8; col++) {
                            if ((spriteByte & (0x80 >> col)) != 0) { // check bit
                                if (setPixel(display, x + col, y + row)) {
                                    V[0xF] = 1; // collision detected
                                }
                            }
                        }
                    }
                    break;
                }
                case 0xE000:
                    switch (instruction & 0x00FF) {
                        case 0x009E: { // Ex9E
                            uint8_t x = (instruction & 0x0F00) >> 8;
                            if (keys[V[x]]) PC += 2;
                            break;
                        }
                        case 0x00A1: { // ExA1
                            uint8_t x = (instruction & 0x0F00) >> 8;
                            if (!keys[V[x]]) PC += 2;
                            break;
                        }
                    }
                    break;
                case 0xF000:
                    switch (instruction & 0x00FF) {
                        case 0x0007:
                            V[(instruction & 0x0F00) >> 8] = DT;
                            break;
                        case 0x000A: { // Fx0A
                            uint8_t x = (instruction & 0x0F00) >> 8;
                            bool keyPressed = false;

                            for (int i = 0; i < 16; i++) {
                                if (keys[i]) {
                                    V[x] = i;
                                    keyPressed = true;
                                    break;
                                }
                            }

                            if (!keyPressed) {
                                PC -= 2; // repeat the instruction until a key is pressed
                            }

                            break;
                        }
                        case 0x0015:
                            DT = V[(instruction & 0x0F00) >> 8];
                            break;
                        case 0x0018:
                            ST = V[(instruction & 0x0F00) >> 8];
                            break;
                        case 0x001E:
                            I += V[(instruction & 0x0F00) >> 8];
                            break;
                        case 0x0029: {
                            uint8_t x = (instruction & 0x0F00) >> 8;  // get register index
                            I = 0x50 + (V[x] * 5);
                            break;
                        }
                        case 0x0033: { // Fx33
                            uint8_t x = (instruction & 0x0F00) >> 8; // extract x
                            uint8_t value = V[x];

                            memory[I]     = value / 100;         // hundreds
                            memory[I + 1] = (value / 10) % 10;  // tens
                            memory[I + 2] = value % 10;         // ones
                            break;
                        }
                        case 0x0055: { // Fx55
                            uint8_t x = (instruction & 0x0F00) >> 8; // extract x
                            for (int i = 0; i <= x; i++) {
                                memory[I + i] = V[i];
                            }
                            break;
                        }
                        case 0x0065: {
                            uint8_t x = (instruction & 0x0F00) >> 8; // which register Vx
                            for (uint8_t i = 0; i <= x; i++) {
                                V[i] = memory[I + i];
                            }
                            break;
                        }
                    }
                }
        }

        
            lastCpu = now;
        }

        // Timers
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimerUpdate);
        if (elapsed.count() >= 16) { // ~60Hz
            if (DT > 0) DT--;
            if (ST > 0) ST--;
            lastTimerUpdate = now;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        int winWidth, winHeight;
        SDL_GetWindowSize(window, &winWidth, &winHeight);

        float scaleX = winWidth / (float)CHIP8_WIDTH;
        float scaleY = winHeight / (float)CHIP8_HEIGHT;
        float scale = std::min(scaleX, scaleY);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        for (int y = 0; y < CHIP8_HEIGHT; y++) {
            for (int x = 0; x < CHIP8_WIDTH; x++) {
                if (display[y * CHIP8_WIDTH + x]) {
                    SDL_Rect rect;
                    rect.x = x * scale + (winWidth - CHIP8_WIDTH * scale) / 2;
                    rect.y = y * scale + (winHeight - CHIP8_HEIGHT * scale) / 2;
                    rect.w = scale;
                    rect.h = scale;
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16); // ~60 FPS
    }


    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}