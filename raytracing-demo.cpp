﻿#include <iostream>
#include <vector>
#include <string> 
#include <random>
#include <stdlib.h>
#include <glm/glm.hpp> // 數學庫支持
#include "svpng.inc" // png 輸出 ref: https://github.com/miloyip/svpng
#include <omp.h> // openmp 多線程加速

using namespace glm;
using namespace std;

// --------------------- end of include --------------------- //

// 采樣次數
const int SAMPLE = 150;
const int SIM_TIME = 150;
const double STEPS = 0.025;

// 每次采樣的亮度
const double BRIGHTNESS = (2.0f * 3.1415926f) * (1.0f / double(SAMPLE));

// 輸出圖像分辨率
const int WIDTH = 256;
const int HEIGHT = 256;

// 相機參數
const double SCREEN_Z = 1.1;        // 視平面 z 坐標
const vec3 EYE = vec3(0, 0, 4.0);   // 相機位置

// 顏色
const vec3 RED(1, 0.5, 0.5);
const vec3 GREEN(0.5, 1, 0.5);
const vec3 BLUE(0.5, 0.5, 1);
const vec3 YELLOW(1.0, 1.0, 0.1);
const vec3 CYAN(0.1, 1.0, 1.0);
const vec3 MAGENTA(1.0, 0.1, 1.0);
const vec3 GRAY(0.5, 0.5, 0.5);
const vec3 WHITE(1, 1, 1);

// --------------- end of global variable definition --------------- //

// 光線
typedef struct Ray
{
    vec3 startPoint = vec3(0, 0, 0);    // 起點
    vec3 direction = vec3(0, 0, 0);     // 方向
}Ray;

// 物體表面材質定義
typedef struct Material
{
    bool isEmissive = false;        // 是否發光
    vec3 normal = vec3(0, 0, 0);    // 法向量
    vec3 color = vec3(0, 0, 0);     // 顏色
    double specularRate = 0.0f;     // 反射光占比
    double roughness = 1.0f;        // 粗糙程度
    double refractRate = 0.0f;      // 折射光占比
    double refractAngle = 1.0f;     // 折射率
    double refractRoughness = 0.0f; // 折射粗糙度
}Material;

// 光線求交結果
typedef struct HitResult
{
    bool isHit = false;             // 是否命中
    double distance = 0.0f;         // 與交點的距離
    vec3 hitPoint = vec3(0, 0, 0);  // 光線命中點
    Material material;              // 命中點的表面材質
}HitResult;

class Shape
{
public:
    Shape() {}
    virtual HitResult intersect(Ray ray) { return HitResult(); }
};

// 三角形
class Triangle : public Shape
{
public:
    Triangle(vec3 P1, vec3 P2, vec3 P3, vec3 C)
    {
        p1 = P1, p2 = P2, p3 = P3;
        material.normal = normalize(cross(p2 - p1, p3 - p1)); material.color = C;
    }
    vec3 p1, p2, p3;    // 三頂點
    Material material;  // 材質

    // 與光線求交
    HitResult intersect(Ray ray)
    {
        HitResult res;

        vec3 S = ray.startPoint;        // 射線起點
        vec3 d = ray.direction;         // 射線方向
        vec3 N = material.normal;       // 法向量
        if (dot(N, d) > 0.0f) N = -N;   // 獲取正確的法向量

        // 距離
        float t = (dot(N, p1) - dot(S, N)) / dot(d, N);
        if (t < 0.0005f) return res;    // 如果三角形在相機背面

        // 交點計算
        vec3 P = S + d * t;

        // 判斷交點是否在三角形中
        vec3 c1 = cross(p2 - p1, P - p1);
        vec3 c2 = cross(p3 - p2, P - p2);
        vec3 c3 = cross(p1 - p3, P - p3);
        vec3 n = material.normal;   // 需要 "原生法向量" 來判斷
        if (dot(c1, n) < 0 || dot(c2, n) < 0 || dot(c3, n) < 0) return res;

        // 裝填返回結果
        res.isHit = true;
        res.distance = t;
        res.hitPoint = P;
        res.material = material;
        res.material.normal = N;    // 要返回正確的法向
        return res;
    };
};

// 球
class Sphere : public Shape
{
public:
    Sphere() {}
    Sphere(vec3 o, double r, vec3 c) { O = o; R = r; material.color = c; }
    vec3 O;             // 圓心
    double R;           // 半徑
    Material material;  // 材質

    // 與光線求交
    HitResult intersect(Ray ray)
    {
        HitResult res;

        vec3 S = ray.startPoint;        // 射線起點
        vec3 d = ray.direction;         // 射線方向

        float OS = length(O - S);
        float SH = dot(O - S, d);
        float OH = sqrt(pow(OS, 2) - pow(SH, 2));

        if (OH > R) return res; // OH大於半徑則不相交

        float PH = sqrt(pow(R, 2) - pow(OH, 2));

        float t1 = length(SH) - PH;
        float t2 = length(SH) + PH;
        float t = (t1 < 0) ? (t2) : (t1);   // 最近距離
        vec3 P = S + t * d;     // 交點

        // 防止自己交自己
        if (fabs(t1) < 0.0005f || fabs(t2) < 0.0005f) return res;

        // 裝填返回結果
        res.isHit = true;
        res.distance = t;
        res.hitPoint = P;
        res.material = material;
        res.material.normal = normalize(P - O); // 要返回正確的法向
        return res;
    }
};

// ---------------------------- end of class definition ---------------------------- //

// 輸出 SRC 數組中的數據到圖像
void imshow(double* SRC, int time)
{
    unsigned char* image = new unsigned char[WIDTH * HEIGHT * 3];// 圖像buffer
    unsigned char* p = image;
    double* S = SRC;    // 源數據
    string file_order = "Images/image_" + to_string(time) +".png";

    FILE* fp;
    fopen_s(&fp, file_order.c_str(), "wb");

    for (int i = 0; i < HEIGHT; i++)
    {
        for (int j = 0; j < WIDTH; j++)
        {
            *p++ = (unsigned char)clamp(pow(*S++, 1.0f / 2.2f) * 255, 0.0, 255.0);  // R 通道
            *p++ = (unsigned char)clamp(pow(*S++, 1.0f / 2.2f) * 255, 0.0, 255.0);  // G 通道
            *p++ = (unsigned char)clamp(pow(*S++, 1.0f / 2.2f) * 255, 0.0, 255.0);  // B 通道
        }
    }

    svpng(fp, WIDTH, HEIGHT, image, 0);
}

// 返回距離最近 hit 的結果
HitResult shoot(vector<Shape*>& shapes, Ray ray)
{
    HitResult res, r;
    res.distance = 1145141919.810f; // inf

    // 遍歷所有圖形，求最近交點
    for (auto& shape : shapes)
    {
        r = shape->intersect(ray);
        if (r.isHit && r.distance < res.distance) res = r;  // 記錄距離最近的求交結果
    }

    return res;
}

// 0-1 隨機數生成
std::uniform_real_distribution<> dis(0.0, 1.0);
random_device rd;
mt19937 gen(rd());
double randf()
{
    return dis(gen);
}

// 單位球內的隨機向量
vec3 randomVec3()
{

    vec3 d;
    do
    {
        d = 2.0f * vec3(randf(), randf(), randf()) - vec3(1, 1, 1);
    } while (dot(d, d) > 1.0);
    return normalize(d);
    /*
    double r1 = randf(), r2 = randf();
    double z = sqrt(1.0f - r2);
    double phi = 2 * 3.1415926 * r1;
    float x = cos(phi) * sqrt(r2);
    float y = sin(phi) * sqrt(r2);
    return normalize(vec3(x, y, z));
    */
}

// 法向半球隨機向量
vec3 randomDirection(vec3 n)
{
    /*
    // 法向半球
    vec3 d;
    do
    {
        d = randomVec3();
    } while (dot(d, n) < 0.0f);
    return d;
    */
    // 法向球
    return normalize(randomVec3() + n);
}

// 路徑追蹤
vec3 pathTracing(vector<Shape*>& shapes, Ray ray, int depth)
{
    if (depth > 8) return vec3(0);
    HitResult res = shoot(shapes, ray);

    if (!res.isHit) return vec3(0); // 未命中

    // 如果發光則返回顏色
    if (res.material.isEmissive) return res.material.color;

    // 有 P 的概率終止
    double r = randf();
    float P = 0.8;
    if (r > P) return vec3(0);

    // 否則繼續
    Ray randomRay;
    randomRay.startPoint = res.hitPoint;
    randomRay.direction = randomDirection(res.material.normal);

    vec3 color = vec3(0);
    float cosine = fabs(dot(-ray.direction, res.material.normal));

    // 根據反射率決定光線最終的方向
    r = randf();
    if (r < res.material.specularRate)  // 鏡面反射
    {
        vec3 ref = normalize(reflect(ray.direction, res.material.normal));
        randomRay.direction = mix(ref, randomRay.direction, res.material.roughness);
        color = pathTracing(shapes, randomRay, depth + 1) * cosine;
    }
    else if (res.material.specularRate <= r && r <= res.material.refractRate)    // 折射
    {
        vec3 ref = normalize(refract(ray.direction, res.material.normal, float(res.material.refractAngle)));
        randomRay.direction = mix(ref, -randomRay.direction, res.material.refractRoughness);
        color = pathTracing(shapes, randomRay, depth + 1) * cosine;
    }
    else    // 漫反射
    {
        vec3 srcColor = res.material.color;
        vec3 ptColor = pathTracing(shapes, randomRay, depth + 1) * cosine;
        color = ptColor * srcColor;    // 和原顏色混合
    }

    return color / P;
}

// ---------------------------- end of functions ---------------------------- //

int main()
{
    double s1_o = -0.7, s2_o = 0, s3_o = 0.7;
    vector<Shape*> shapes;  // 幾何物體的集合

    Sphere s1 = Sphere(vec3(-0.65, s1_o, 0.0), 0.3, GREEN);
    Sphere s2 = Sphere(vec3(0.0, s2_o, 0.0), 0.4, WHITE);
    Sphere s3 = Sphere(vec3(0.65, s3_o, 0.0), 0.3, BLUE);
    s1.material.specularRate = 0.3;
    s1.material.roughness = 0.1;

    s2.material.specularRate = 0.3; // 反射光占比
    s2.material.refractRate = 0.95;
    s2.material.refractAngle = 0.1;
    //s2.material.refractRoughness = 0.05;

    s3.material.specularRate = 0.3;

    shapes.push_back(&s1);
    shapes.push_back(&s2);
    shapes.push_back(&s3);

    shapes.push_back(new Triangle(vec3(-0.15, 0.6, -0.6), vec3(-0.15, -0.6, -0.6), vec3(0.15, 0.6, -0.6), YELLOW));
    shapes.push_back(new Triangle(vec3(0.15, 0.6, -0.6), vec3(-0.15, -0.6, -0.6), vec3(0.15, -0.6, -0.6), YELLOW));

    Triangle tt = Triangle(vec3(-0.2, -0.2, -0.95), vec3(0.2, -0.2, -0.95), vec3(-0.0, -0.9, 0.4), YELLOW);
    //tt.material.specularRate = 0.1;
    //tt.material.refractRate = 0.85;
    //tt.material.refractRoughness = 0.3;
    //shapes.push_back(&tt);

    // 發光物
    Triangle a1 = Triangle(vec3(-1, 1, 1), vec3(-0.5, 1, 1), vec3(-1, 1, 0.5), WHITE);
    Triangle a2 = Triangle(vec3(-1, 1, 0.5), vec3(-0.5, 1, 1), vec3(-0.5, 1, 0.5), WHITE);
    Triangle b1 = Triangle(vec3(1, 1, 1), vec3(0.5, 1, 1), vec3(1, 1, 0.5), WHITE);
    Triangle b2 = Triangle(vec3(1, 1, 0.5), vec3(0.5, 1, 1), vec3(0.5, 1, 0.5), WHITE);
    Triangle c1 = Triangle(vec3(-1, 1, -1), vec3(-1, 1, -0.5), vec3(-0.5, 1, -1), WHITE);
    Triangle c2 = Triangle(vec3(-1, 1, -0.5), vec3(-0.5, 1, -0.5), vec3(-0.5, 1, -1), WHITE);
    Triangle d1 = Triangle(vec3(1, 1, -1), vec3(1, 1, -0.5), vec3(0.5, 1, -1), WHITE);
    Triangle d2 = Triangle(vec3(1, 1, -0.5), vec3(0.5, 1, -0.5), vec3(0.5, 1, -1), WHITE);

    a1.material.isEmissive = true;
    a2.material.isEmissive = true;
    b1.material.isEmissive = true;
    b2.material.isEmissive = true;
    c1.material.isEmissive = true;
    c2.material.isEmissive = true;
    d1.material.isEmissive = true;
    d2.material.isEmissive = true;
    shapes.push_back(&a1);
    shapes.push_back(&a2);
    shapes.push_back(&b1);
    shapes.push_back(&b2);
    shapes.push_back(&c1);
    shapes.push_back(&c2);
    shapes.push_back(&d1);
    shapes.push_back(&d2);

    // 背景盒子
    // bottom
    shapes.push_back(new Triangle(vec3(1, -1, 1), vec3(-1, -1, -1), vec3(-1, -1, 1), WHITE));
    shapes.push_back(new Triangle(vec3(1, -1, 1), vec3(1, -1, -1), vec3(-1, -1, -1), WHITE));
    // top
    shapes.push_back(new Triangle(vec3(1, 1, 1), vec3(-1, 1, 1), vec3(-1, 1, -1), WHITE));
    shapes.push_back(new Triangle(vec3(1, 1, 1), vec3(-1, 1, -1), vec3(1, 1, -1), WHITE));
    // back
    shapes.push_back(new Triangle(vec3(1, -1, -1), vec3(-1, 1, -1), vec3(-1, -1, -1), CYAN));
    shapes.push_back(new Triangle(vec3(1, -1, -1), vec3(1, 1, -1), vec3(-1, 1, -1), CYAN));
    // left
    shapes.push_back(new Triangle(vec3(-1, -1, -1), vec3(-1, 1, 1), vec3(-1, -1, 1), BLUE));
    shapes.push_back(new Triangle(vec3(-1, -1, -1), vec3(-1, 1, -1), vec3(-1, 1, 1), BLUE));
    // right
    shapes.push_back(new Triangle(vec3(1, 1, 1), vec3(1, -1, -1), vec3(1, -1, 1), RED));
    shapes.push_back(new Triangle(vec3(1, -1, -1), vec3(1, 1, 1), vec3(1, 1, -1), RED));

    int time = 0;
    int sig1 = 1, sig2 = 1, sig3 = -1;
    int sig_light = 1;
    
    while (time <= SIM_TIME)
    {
        double* image = new double[WIDTH * HEIGHT * 3];
        memset(image, 0.0, sizeof(double) * WIDTH * HEIGHT * 3);

        omp_set_num_threads(200); // 線程個數
        #pragma omp parallel for
        for (int k = 0; k < SAMPLE; k++)
        {
            double* p = image;
            for (int i = 0; i < HEIGHT; i++)
            {
                for (int j = 0; j < WIDTH; j++)
                {
                    // 像素坐標轉投影平面坐標
                    double x = 2.0 * double(j) / double(WIDTH) - 1.0;
                    double y = 2.0 * double(HEIGHT - i) / double(HEIGHT) - 1.0;

                    // MSAA
                    x += (randf() - 0.5f) / double(WIDTH);
                    y += (randf() - 0.5f) / double(HEIGHT);

                    vec3 coord = vec3(x, y, SCREEN_Z);          // 計算投影平面坐標
                    vec3 direction = normalize(coord - EYE);    // 計算光線投射方向

                    // 生成光線
                    Ray ray;
                    ray.startPoint = coord;
                    ray.direction = direction;

                    // 與場景的交點
                    HitResult res = shoot(shapes, ray);
                    vec3 color = vec3(0, 0, 0);

                    if (res.isHit)
                    {
                        // 命中光源直接返回光源顏色
                        if (res.material.isEmissive)
                        {
                            color = res.material.color;
                        }
                        // 命中實體則選擇一個隨機方向重新發射光線並且進行路徑追蹤
                        else
                        {
                            // 根據交點處法向量生成交點處反射的隨機半球向量
                            Ray randomRay;
                            randomRay.startPoint = res.hitPoint;
                            randomRay.direction = randomDirection(res.material.normal);

                            // 根據反射率決定光線最終的方向
                            double r = randf();
                            if (r < res.material.specularRate)  // 鏡面反射
                            {
                                vec3 ref = normalize(reflect(ray.direction, res.material.normal));
                                randomRay.direction = mix(ref, randomRay.direction, res.material.roughness);
                                color = pathTracing(shapes, randomRay, 0);
                            }
                            else if (res.material.specularRate <= r && r <= res.material.refractRate)    // 折射
                            {
                                vec3 ref = normalize(refract(ray.direction, res.material.normal, float(res.material.refractAngle)));
                                randomRay.direction = mix(ref, -randomRay.direction, res.material.refractRoughness);
                                color = pathTracing(shapes, randomRay, 0);
                            }
                            else    // 漫反射
                            {
                                vec3 srcColor = res.material.color;
                                vec3 ptColor = pathTracing(shapes, randomRay, 0);
                                color = ptColor * srcColor;    // 和原顏色混合
                            }
                            color *= BRIGHTNESS;
                        }
                    }

                    *p += color.x; p++;  // R 通道
                    *p += color.y; p++;  // G 通道
                    *p += color.z; p++;  // B 通道
                }
            }
        }

        imshow(image, time);
        
        // Objects moving
        if (s1_o > 0.7 || s1_o < -0.7) sig1 = -sig1;
        s1_o += sig1 * STEPS;

        if (s2_o > 0.6 || s2_o < -0.6) sig2 = -sig2;
        s2_o += sig2 * STEPS * 2;

        if (s3_o > 0.7 || s3_o < -0.7) sig3 = -sig3;
        s3_o += sig3 * STEPS;

        s1.O = vec3(-0.65, s1_o, 0.0);
        s2.O = vec3(0.0, s2_o, 0.0);
        s3.O = vec3(0.65, s3_o, 0.0);

        cout << "time: " << time << endl;
        time += 1;
    }

    return 0;
}