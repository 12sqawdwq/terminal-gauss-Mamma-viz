#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef struct {
    int r; int g; int b;
} Color;

float func(float x, float y) {
    float r1_sq = (x - 4) * (x - 4) + (y - 4) * (y - 4);
    float r2_sq = (x + 4) * (x + 4) + (y + 4) * (y + 4);
    
    // 剔除远处，提高效率
    if (r1_sq > 250 && r2_sq > 250) return 0.0f;

    float r1_quad = r1_sq * r1_sq;
    float r2_quad = r2_sq * r2_sq;
    
    return exp(-r1_quad / 1000.0f) + 
           exp(-r2_quad / 1000.0f) + 
           0.1f * exp(-r2_quad) + 
           0.1f * exp(-r1_quad);
}

int main() {
    // 12级肉质色阶：保证高饱和度，不再变黑
    Color palette[12] = {
        {60,  30,  30},  // 0: 底部深褐 (远)
        {100, 60,  50},  // 1: 褐
        {180, 130, 110}, // 2: 灰粉
        {210, 160, 140}, // 3: 浅肉
        {230, 180, 160}, // 4: 亮肉
        {240, 140, 120}, // 5: 嫩粉
        {220, 100, 80},  // 6: 充血粉
        {200, 60,  50},  // 7: 鲜红
        {180, 40,  30},  // 8: 深红
        {160, 20,  20},  // 9: 暗红
        {140, 0,   0},   // 10: 极深红
        {255, 50,  50}   // 11: 溃烂/高亮
    };

    // 字符表不再代表光照，而是代表“距离/厚度”
    // 越靠左越厚实（近处），越靠右越稀疏（远处）
    char depth_chars[] = "@%#*+=-:. "; 
    
    float A = 0, B = 0;

    printf("\x1b[2J"); 

    while (1) {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int width = w.ws_col;
        int height = w.ws_row;
        if (width > 220) width = 220;
        if (height > 110) height = 110;

        float K1 = width * 0.4f;

        float zbuffer[width * height];
        Color pixel_colors[width * height];
        char char_buffer[width * height];

        // 初始化
        for(int k=0; k<width*height; k++) {
            zbuffer[k] = 0.0f;
            char_buffer[k] = ' '; // 背景透明
            pixel_colors[k] = (Color){0,0,0};
        }

        float sinA = sin(A), cosA = cos(A);
        float sinB = sin(B), cosB = cos(B);

        // --- 极高密度 ---
        // 步长 0.03，保证即使旋转时也不会出现缝隙
        for (float i = -13.0f; i < 13.0f; i += 0.03f) {
            for (float j = -13.0f; j < 13.0f; j += 0.03f) {
                
                float z_val = func(i, j);
                
                float x = i;
                float y = j;
                float z = z_val * 6.5f; // 高度夸张

                // 旋转
                float y1 = y * cosA - z * sinA;
                float z1 = y * sinA + z * cosA;
                float x2 = x * cosB - y1 * sinB;
                float y2 = x * sinB + y1 * cosB;
                float z2 = z1;

                // 摄像机距离 
                float camera_dist = z2 + 40.0f;
                float ooz = 1.0f / camera_dist; 

                int xp = (int)(width / 2 + K1 * ooz * x2 * 2.0);
                int yp = (int)(height / 2 - K1 * ooz * y2);
                int idx = xp + yp * width;

                if (idx >= 0 && idx < width * height) {
                    if (ooz > zbuffer[idx]) {
                        zbuffer[idx] = ooz;
                        
                        // --- 策略 1: 颜色只看高度 (Height) ---
                        // 这样能保证山峰永远是红的，底座永远是肉色的，不会因为旋转变黑
                        int c_idx = (int)(z_val * 9.5f);
                        if (c_idx < 0) c_idx = 0;
                        if (c_idx > 11) c_idx = 11;
                        
                        Color c = palette[c_idx];

                        // --- 策略 2: 字符只看距离 (Depth) ---
                        // 离摄像机越近(camera_dist越小)，用越实的字符(@)
                        // 离摄像机越远，用越虚的字符(.)
                        // 距离大概在 25.0 到 55.0 之间变化
                        float depth_norm = (camera_dist - 25.0f) / 30.0f; 
                        if (depth_norm < 0) depth_norm = 0;
                        if (depth_norm > 1) depth_norm = 1;
                        
                        int char_idx = (int)(depth_norm * 9.0f); 
                        if (char_idx > 8) char_idx = 8;
                        
                        char_buffer[idx] = depth_chars[char_idx];
                        
                        // --- 策略 3: 简单的深度变暗 (Fake Fog) ---
                        // 让远处的颜色稍微暗一点，增加层次感
                        float dim_factor = 1.0f - (depth_norm * 0.4f); // 远处变暗 40%
                        pixel_colors[idx].r = (int)(c.r * dim_factor);
                        pixel_colors[idx].g = (int)(c.g * dim_factor);
                        pixel_colors[idx].b = (int)(c.b * dim_factor);
                    }
                }
            }
        }

        // 渲染
        printf("\x1b[H"); 
        for (int k = 0; k < width * height; k++) {
            if (k % width == 0 && k != 0) putchar('\n');
            
            if (char_buffer[k] != ' ') {
                printf("\x1b[38;2;%d;%d;%dm%c", 
                       pixel_colors[k].r, pixel_colors[k].g, pixel_colors[k].b, 
                       char_buffer[k]);
            } else {
                printf("\x1b[0m ");
            }
        }
        
        printf("\x1b[0m");
        fflush(stdout); 

        A += 0.01f; 
        B += 0.015f;
        
        usleep(1000); 
    }
    return 0;
}
