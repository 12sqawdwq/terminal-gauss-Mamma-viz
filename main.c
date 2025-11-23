#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/ioctl.h>

// 基础摄像机距离
#define BASE_CAM_DIST 60.0f 

typedef struct {
    int r, g, b;
} Color;

// 1. 数学方程 (双高斯分布模型)
// 几何上模拟双侧乳房形态
float func(float x, float y) {
    float r1_sq = (x - 4) * (x - 4) + (y - 4) * (y - 4);
    float r2_sq = (x + 4) * (x + 4) + (y + 4) * (y + 4);
    float r1_quad = r1_sq * r1_sq;
    float r2_quad = r2_sq * r2_sq;
    
    // 0.1f * exp(...) 是模拟乳头的突起
    return exp(-r1_quad / 1000.0f) + exp(-r2_quad / 1000.0f) +
           0.1f * exp(-r2_quad) + 0.1f * exp(-r1_quad);
}

// 2. 光照计算 (保持不变，用于计算立体感)
float get_lighting(float x, float y) {
    float eps = 0.05f;
    float z0 = func(x, y);
    float nx = -((func(x + eps, y) - z0) / eps);
    float ny = -((func(x, y + eps) - z0) / eps);
    float nz = 1.0f;

    // 光源位置：左上方，模拟无影灯效果
    float lx = -0.5f; float ly = -0.5f; float lz = 1.0f;
    
    float dot = nx * lx + ny * ly + nz * lz;
    float len_n = sqrt(nx*nx + ny*ny + nz*nz);
    float len_l = sqrt(lx*lx + ly*ly + lz*lz);
    
    float intensity = dot / (len_n * len_l);
    // 环境光设为 0.3，避免阴影过黑，模拟生物组织的通透感
    return 0.3f + 0.7f * (intensity > 0 ? intensity : 0);
}

// 3. 关键修改：生物组织配色方案 (Medical Tissue Shader)
Color get_tissue_color(float z, float light) {
    Color base;
    
    // --- 色相映射 (Anatomical Mapping) ---
    
    // 阶段 1: 正常皮肤 (Base Skin) - 模拟脂肪/腺体覆盖区域
    // RGB: (235, 190, 170) -> (220, 170, 150)
    if (z < 0.5f) {
        float t = z / 0.5f;
        base.r = (int)(235 + (220 - 235) * t);
        base.g = (int)(190 + (170 - 190) * t);
        base.b = (int)(170 + (150 - 170) * t);
    } 
    // 阶段 2: 乳晕过渡区 (Areola Transition)
    // RGB: (220, 170, 150) -> (180, 100, 100)
    else if (z < 0.9f) {
        float t = (z - 0.5f) / 0.4f;
        base.r = (int)(220 + (180 - 220) * t);
        base.g = (int)(170 + (100 - 170) * t);
        base.b = (int)(150 + (100 - 150) * t);
    } 
    // 阶段 3: 乳头区域 (Nipple) - 色素沉着较深
    // RGB: (180, 100, 100) -> (160, 60, 60)
    else {
        float t = (z - 0.9f) / 0.3f; 
        if(t > 1) t = 1;
        base.r = (int)(180 + (160 - 180) * t);
        base.g = (int)(100 + (60 - 100) * t);
        base.b = (int)(100 + (60 - 100) * t);
    }

    // --- 光照调制 (Lighting & Subsurface Scattering) ---
    
    float shadow = 0.5f + 0.5f * light; // 阴影不纯黑
    
    if (light > 0.92f) { 
        // 高光 (Specular): 模拟皮肤油脂反光，微弱的泛白
        float spec = (light - 0.92f) / 0.08f;
        // 高光偏暖白，不是纯白
        base.r += (int)((255 - base.r) * spec * 0.6);
        base.g += (int)((240 - base.g) * spec * 0.6);
        base.b += (int)((230 - base.b) * spec * 0.6);
    } else {
        // 阴影处理：
        // 模拟次表面散射(Subsurface Scattering)：阴影处饱和度较高，偏红棕色，而不是灰黑色
        base.r = (int)(base.r * shadow);
        base.g = (int)(base.g * (shadow * 0.9)); // 绿色衰减更多，使阴影偏红
        base.b = (int)(base.b * (shadow * 0.8)); // 蓝色衰减最多
    }

    if(base.r>255) base.r=255; if(base.g>255) base.g=255; if(base.b>255) base.b=255;
    if(base.r<0) base.r=0;     if(base.g<0) base.g=0;     if(base.b<0) base.b=0;
    
    return base;
}

int main() {
    float A = 0, B = 0;
    
    float *zbuffer = NULL;
    char *char_buffer = NULL;
    Color *color_buffer = NULL;
    char *output_frame = NULL;
    
    int current_w = 0, current_h = 0;

    // 字符梯度：使用较柔和的字符，避免过于锐利的机械感
    char chars[] = ".,:;!*xo#%&@"; 
    int char_count = 11;

    printf("\x1b[2J\x1b[?25l");

    while (1) {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int width = w.ws_col;
        int height = w.ws_row;
        if (width <= 0) width = 80;
        if (height <= 0) height = 40;

        if (width != current_w || height != current_h) {
            current_w = width;
            current_h = height;
            if (zbuffer) free(zbuffer);
            if (char_buffer) free(char_buffer);
            if (color_buffer) free(color_buffer);
            if (output_frame) free(output_frame);

            zbuffer = (float *)malloc(width * height * sizeof(float));
            char_buffer = (char *)malloc(width * height * sizeof(char));
            color_buffer = (Color *)malloc(width * height * sizeof(Color));
            output_frame = (char *)malloc(width * height * 32 + height * 8);
        }
        memset(char_buffer, ' ', width * height);
        memset(zbuffer, 0, width * height * sizeof(float));

        float K1 = (float)height * 2.0f;
        float step_size = 5.5f / (float)height; 
        if (step_size > 0.1f) step_size = 0.1f;
        if (step_size < 0.04f) step_size = 0.04f;

        for (float i = -14.0f; i < 14.0f; i += step_size) {
            for (float j = -14.0f; j < 14.0f; j += step_size) {
                
                float z_val = func(i, j);
                float light = get_lighting(i, j);

                float x = i;
                float y = j;
                float z = z_val * 4.0f;

                float sinA = sin(A), cosA = cos(A);
                float sinB = sin(B), cosB = cos(B);

                float y1 = y * cosA - z * sinA;
                float z1 = y * sinA + z * cosA;
                float x2 = x * cosB - y1 * sinB;
                float y2 = x * sinB + y1 * cosB;
                float z2 = z1;

                float ooz = 1.0f / (z2 + BASE_CAM_DIST);

                int xp = (int)(width / 2 + K1 * ooz * x2 * 2.0);
                int yp = (int)(height / 2 - K1 * ooz * y2);
                int idx = xp + yp * width;

                if (idx >= 0 && idx < width * height) {
                    if (ooz > zbuffer[idx]) {
                        zbuffer[idx] = ooz;
                        
                        int lum = (int)(light * char_count); 
                        if (lum < 0) lum = 0; else if (lum > char_count) lum = char_count;
                        char_buffer[idx] = chars[lum];
                        
                        // 使用生物组织配色
                        color_buffer[idx] = get_tissue_color(z_val, light);
                    }
                }
            }
        }

        int ptr = 0;
        ptr += sprintf(output_frame + ptr, "\x1b[H");

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = x + y * width;
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
        output_frame[ptr] = '\0';
        
        fwrite(output_frame, 1, ptr, stdout);

        A += 0.02f;
        B += 0.03f;
        
        usleep(16000);
    }

    return 0;
}
