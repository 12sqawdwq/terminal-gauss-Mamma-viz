#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- 平台相关的终端尺寸检测 ---
#ifdef _WIN32
    #include <windows.h>
    void usleep(__int64 usec) {
        Sleep((DWORD)(usec / 1000));
    }

    // Windows 获取终端宽高
    void get_terminal_size(int *width, int *height) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int columns, rows;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        // 保护措施，防止获取失败
        *width = columns > 0 ? columns : 80;
        *height = rows > 0 ? rows : 40;
    }

    void setup_console() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
        SetConsoleMode(hOut, dwMode);
        system("cls");
    }
#else
    #include <unistd.h>
    #include <sys/ioctl.h>

    // Linux 获取终端宽高
    void get_terminal_size(int *width, int *height) {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        *width = w.ws_col > 0 ? w.ws_col : 80;
        *height = w.ws_row > 0 ? w.ws_row : 40;
    }

    void setup_console() {
        // Linux 不需要特殊设置 VT 模式
    }
#endif
// ------------------------------

typedef struct {
    int r, g, b;
} Color;

// 数学模型
float func(float x, float y) {
    float r1_sq = (x - 4) * (x - 4) + (y - 4) * (y - 4);
    float r2_sq = (x + 4) * (x + 4) + (y + 4) * (y + 4);
    float r1_quad = r1_sq * r1_sq;
    float r2_quad = r2_sq * r2_sq;
    return exp(-r1_quad / 1000.0f) + exp(-r2_quad / 1000.0f) +
           0.1f * exp(-r2_quad) + 0.1f * exp(-r1_quad);
}

// 光照计算
float get_lighting(float x, float y) {
    float eps = 0.05f;
    float z0 = func(x, y);
    float nx = -((func(x + eps, y) - z0) / eps);
    float ny = -((func(x, y + eps) - z0) / eps);
    float nz = 1.0f;

    // 光源位置调整：左上方高处
    float lx = -0.5f; float ly = -0.5f; float lz = 1.0f;
    float dot = nx * lx + ny * ly + nz * lz;
    float len_n = sqrt(nx*nx + ny*ny + nz*nz);
    float len_l = sqrt(lx*lx + ly*ly + lz*lz);

    float intensity = dot / (len_n * len_l);
    return 0.2f + 0.8f * (intensity > 0 ? intensity : 0);
}

// 颜色混合逻辑
Color get_hybrid_color(float z, float light) {
    Color base;
    // Arch/Hyprland/Glassmorphism 配色
    if (z < 0.4f) {
        float t = z / 0.4f;
        base.r = (int)(30 + (137 - 30) * t);
        base.g = (int)(30 + (220 - 30) * t);
        base.b = (int)(46 + (235 - 46) * t);
    } else if (z < 0.8f) {
        float t = (z - 0.4f) / 0.4f;
        base.r = (int)(137 + (245 - 137) * t);
        base.g = (int)(220 + (194 - 220) * t);
        base.b = (int)(235 + (231 - 235) * t);
    } else {
        float t = (z - 0.8f) / 0.4f; if(t > 1) t = 1;
        base.r = (int)(245 + (255 - 245) * t);
        base.g = (int)(194 + (255 - 194) * t);
        base.b = (int)(231 + (255 - 231) * t);
    }

    float shadow = 0.3f + 0.7f * light;
    if (light > 0.95f) { // 高光
        float spec = (light - 0.95f) / 0.05f;
        base.r += (int)((255 - base.r) * spec);
        base.g += (int)((255 - base.g) * spec);
        base.b += (int)((255 - base.b) * spec);
    } else {
        base.r = (int)(base.r * shadow);
        base.g = (int)(base.g * shadow);
        base.b = (int)(base.b * shadow);
    }
    // Clamp
    if(base.r>255) base.r=255; if(base.g>255) base.g=255; if(base.b>255) base.b=255;
    return base;
}

int main() {
    float A = 0, B = 0;

    // 动态指针
    float *zbuffer = NULL;
    char *char_buffer = NULL;
    Color *color_buffer = NULL;
    char *output_frame = NULL;

    int current_width = 0;
    int current_height = 0;

    char chars[] = ".,-~:;=!*#$@";
    int char_count = 11;

    setup_console();
    printf("\x1b[?25l"); // 隐藏光标

    while (1) {
        // 1. 获取当前窗口大小
        int w, h;
        get_terminal_size(&w, &h);

        // 2. 如果窗口大小发生变化，重新分配内存
        if (w != current_width || h != current_height) {
            current_width = w;
            current_height = h;

            if (zbuffer) free(zbuffer);
            if (char_buffer) free(char_buffer);
            if (color_buffer) free(color_buffer);
            if (output_frame) free(output_frame);

            zbuffer = (float *)malloc(w * h * sizeof(float));
            char_buffer = (char *)malloc(w * h * sizeof(char));
            color_buffer = (Color *)malloc(w * h * sizeof(Color));
            // 预估每像素30字节 (转义码) + 行尾换行符
            output_frame = (char *)malloc(w * h * 30 + h * 10);
        }

        // 清空 Buffer
        memset(char_buffer, ' ', w * h);
        memset(zbuffer, 0, w * h * sizeof(float));

        // 3. 动态计算缩放比例 (Zoom)
        // 核心逻辑：让 K1 (物体大小) 随屏幕高度 h 线性增长
        // 系数 0.8 决定了物体占据屏幕高度的百分比
        // 摄像机距离 (Camera Distance) 也需要随 K1 调整

        float K1 = (float)h * 2.0f; // 缩放系数
        float cam_dist = 60.0f;     // 摄像机拉远一点以适应大视野

        // 4. 动态调整采样密度
        // 屏幕越大，为了防止出现黑点缝隙，step 必须越小
        // 反之，屏幕小的时候 step 大一点以节省 CPU
        float step_size = 6.0f / (float)h;
        if (step_size > 0.1f) step_size = 0.1f; // 下限
        if (step_size < 0.05f) step_size = 0.05f; // 上限(保护性能)

        for (float i = -14.0f; i < 14.0f; i += step_size) {
            for (float j = -14.0f; j < 14.0f; j += step_size) {

                float z_val = func(i, j);
                float light = get_lighting(i, j);

                float x = i;
                float y = j;
                float z = z_val * 4.0f; // 高度拉伸

                // 旋转
                float sinA = sin(A), cosA = cos(A);
                float sinB = sin(B), cosB = cos(B);

                float y1 = y * cosA - z * sinA;
                float z1 = y * sinA + z * cosA;
                float x2 = x * cosB - y1 * sinB;
                float y2 = x * sinB + y1 * cosB;
                float z2 = z1;

                // 投影
                float ooz = 1.0f / (z2 + cam_dist);

                // x2 * 2.0 是为了修正字符的高宽比 (大约 1:2)
                // 使得正方形看起来像正方形
                int xp = (int)(w / 2 + K1 * ooz * x2 * 2.0);
                int yp = (int)(h / 2 - K1 * ooz * y2);

                int idx = xp + yp * w;

                if (idx >= 0 && idx < w * h) {
                    if (ooz > zbuffer[idx]) {
                        zbuffer[idx] = ooz;

                        int lum = (int)(light * char_count);
                        if (lum < 0) lum = 0; else if (lum > char_count) lum = char_count;
                        char_buffer[idx] = chars[lum];
                        color_buffer[idx] = get_hybrid_color(z_val, light);
                    }
                }
            }
        }

        // 5. 输出帧
        int ptr = 0;
        ptr += sprintf(output_frame + ptr, "\x1b[H");

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = x + y * w;
                char c = char_buffer[idx];

                if (c != ' ') {
                    Color col = color_buffer[idx];
                    ptr += sprintf(output_frame + ptr, "\x1b[38;2;%d;%d;%dm%c", col.r, col.g, col.b, c);
                } else {
                    ptr += sprintf(output_frame + ptr, "\x1b[0m ");
                }
            }
            ptr += sprintf(output_frame + ptr, "\x1b[0m\n");
        }

        // 确保字符串结束符
        output_frame[ptr] = '\0';

        // 一次性打印
        fwrite(output_frame, 1, ptr, stdout);

        A += 0.02f;
        B += 0.03f;
        usleep(16000); // 60fps
    }

    return 0;
}