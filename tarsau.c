#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>

#define MAX_FILES 32
#define MAX_TOTAL_SIZE (200 * 1024 * 1024) // 200 MB

// Dosyanın metin (ASCII) dosyası olup olmadığını kontrol eder (karakter başına 1 bayt kuralı)
int is_ascii_file(const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) return 0;

    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (ch > 127 || (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t')) {
            fclose(file);
            return 0; 
        }
    }
    fclose(file);
    return 1; 
}

// Dosyanın .sau uzantılı olup olmadığını kontrol eder
int is_sau_file(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    return strcmp(dot, ".sau") == 0;
}

// -b (Arşivleme) İşlemi
void archive_files(int file_count, char *files[], const char *output_file) {
    long total_size = 0;
    char metadata[16384] = ""; // Organizasyon verisini tutar
    
    for (int i = 0; i < file_count; i++) {
        // Yalnızca metin (ASCII) kontrolü
        if (!is_ascii_file(files[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            exit(EXIT_FAILURE);
        }

        // Dosya boyutu ve okuma/yazma/çalıştırma izinlerini çekiyoruz
        struct stat st;
        if (stat(files[i], &st) == -1) {
            perror("Dosya bilgileri okunamadi");
            exit(EXIT_FAILURE);
        }

        total_size += st.st_size;
        // Toplam 200 MB sınırı kontrolü
        if (total_size > MAX_TOTAL_SIZE) {
            printf("Hata: Toplam dosya boyutu 200 MB'i gecemez!\n");
            exit(EXIT_FAILURE);
        }

        // Organizasyon formatı: |Dosya adı, izinler, boyut| 
        char temp_meta[512];
        sprintf(temp_meta, "|%s,%04o,%ld|", files[i], st.st_mode & 07777, (long)st.st_size);
        strcat(metadata, temp_meta);
    }

    size_t metadata_len = strlen(metadata);
    size_t total_header_size = 10 + metadata_len;

    FILE *out = fopen(output_file, "wb");
    if (!out) {
        perror("Cikti dosyasi olusturulamadi");
        exit(EXIT_FAILURE);
    }

    // İlk 10 bayta boyutu yazıyoruz 
    fprintf(out, "%010zu", total_header_size);
    // Kayıtları yazıyoruz 
    fprintf(out, "%s", metadata);

    // Arşivlenmiş dosyalar (ASCII formatında art arda) 
    for (int i = 0; i < file_count; i++) {
        FILE *in = fopen(files[i], "rb");
        if (in) {
            char buffer[4096];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), in)) > 0) {
                fwrite(buffer, 1, bytes_read, out);
            }
            fclose(in);
        }
    }

    fclose(out);
printf("Basarili: %s arsiv dosyasi olusturuldu.\n", output_file);}

// -a (Arşivden Çıkarma) İşlemi
void extract_files(const char *archive_file, const char *target_dir) {
    if (!is_sau_file(archive_file)) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(EXIT_FAILURE);
    }

    FILE *in = fopen(archive_file, "rb");
    if (!in) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(EXIT_FAILURE);
    }

    char current_dir[1024] = ".";
    // Dizin kontrolü ve oluşturma 
    if (target_dir != NULL) {
        struct stat st = {0};
        if (stat(target_dir, &st) == -1) {
            if (mkdir(target_dir, 0777) == -1) {
                perror("Dizin olusturulamadi");
                exit(EXIT_FAILURE);
            }
        }
        strcpy(current_dir, target_dir);
    }

    // İlk 10 bayttan sayısal boyutu okuma 
    char header_size_str[11];
    if (fread(header_size_str, 1, 10, in) != 10) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(in);
        exit(EXIT_FAILURE);
    }
    header_size_str[10] = '\0';
    size_t total_header_size = (size_t)atol(header_size_str);

    size_t metadata_size = total_header_size - 10;
    char *metadata = (char *)malloc(metadata_size + 1);
    
    if (fread(metadata, 1, metadata_size, in) != metadata_size) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata);
        fclose(in);
        exit(EXIT_FAILURE);
    }
    metadata[metadata_size] = '\0';

    char file_names[MAX_FILES][256];
    long file_sizes[MAX_FILES];
    int file_perms[MAX_FILES];
    int count = 0;

    // Organizasyon kayıtlarını virgülle ayıklama 
    char *cursor = metadata;
    while (*cursor != '\0') {
        if (*cursor == '|') {
            cursor++;
            if (*cursor == '\0') break;

            char record[512] = {0};
            int i = 0;
            while (*cursor != '|' && *cursor != '\0' && i < 511) {
                record[i++] = *cursor++;
            }
            record[i] = '\0';

            // || yan yana gelirse oluşan boş kayıtları atlama (Çökme önleyici)
            if (strlen(record) == 0) continue;

            char *token = strtok(record, ",");
            if (token) strcpy(file_names[count], token);
            
            token = strtok(NULL, ",");
            if (token) file_perms[count] = (int)strtol(token, NULL, 8);
            
            token = strtok(NULL, ",");
            if (token) file_sizes[count] = atol(token);

            count++;
        } else {
            cursor++;
        }
    }

    // Dosyaları dizine çıkartma ve izinleri koruma 
    for (int i = 0; i < count; i++) {
        char target_path[1024];
        snprintf(target_path, sizeof(target_path), "%s/%s", current_dir, file_names[i]);

        FILE *out = fopen(target_path, "wb");
        if (!out) continue;

        long bytes_left = file_sizes[i];
        char buffer[4096];
        while (bytes_left > 0) {
            size_t to_read = (bytes_left < (long)sizeof(buffer)) ? bytes_left : sizeof(buffer);
            size_t bytes_read = fread(buffer, 1, to_read, in);
            if (bytes_read == 0) break;
            fwrite(buffer, 1, bytes_read, out);
            bytes_left -= bytes_read;
        }
        fclose(out);

        // Orijinal izinleri geri atama 
        chmod(target_path, file_perms[i]);
    }
    
    // Arkadaşlarının formatındaki başarı mesajı
    printf("Basarili: Arsiv basariyla %s/ dizinine cikarildi.\n", target_dir ? target_dir : ".");

    free(metadata);
    fclose(in);
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Kullanim hatasi! Eksik arguman girdiniz.\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "-b") == 0) {
        char *input_files[MAX_FILES];
        int file_count = 0;
        char *output_file = "a.sau"; // Varsayılan arşiv adı 

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    output_file = argv[i + 1];
                    break;
                } else {
                    printf("Hata: -o parametresinden sonra dosya adi belirtilmedi!\n");
                    exit(EXIT_FAILURE);
                }
            } else {
                if (file_count >= MAX_FILES) {
                    printf("Hata: Toplam giris dosyasi sayisi en fazla 32 olabilir.\n");
                    exit(EXIT_FAILURE);
                }
                input_files[file_count] = argv[i];
                file_count++;
            }
        }

        if (file_count == 0) {
            printf("Hata: Birlestirilecek dosya belirtilmedi!\n");
            exit(EXIT_FAILURE);
        }
        archive_files(file_count, input_files, output_file);
    } 
    else if (strcmp(argv[1], "-a") == 0) {
        if (argc > 4) {
            printf("Hata: -a parametresi en fazla 2 arguman alabilir!\n");
            exit(EXIT_FAILURE);
        }

        const char *archive_file = argv[2];
        const char *target_dir = (argc == 4) ? argv[3] : NULL;

        extract_files(archive_file, target_dir);
    } 
    else {
        printf("Hatali parametre! Sadece -b veya -a kullanilabilir.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}