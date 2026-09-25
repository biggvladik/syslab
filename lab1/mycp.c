#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

#define BUFFER_SIZE 65536


int main(int argc, char *argv[]) {
	
	if (argc != 3){
		const char msg[] = "Usgae ./mycp source destination\n";
		write(STDERR_FILENO, msg, sizeof(msg) - 1);
		return 1;
		
		}
		
	const char *source = argv[1];
	const char *destination = argv[2];
	
	int src_fd = open(source, O_RDONLY);
	
	if (src_fd == -1) {
		perror("open source");
		return 1;
		}
		
	struct stat st;
	
	if (fstat(src_fd, &st) == -1)  {
		perror("fstat");
		close(src_fd);
		return 1;
		}
	
	off_t size = lseek(src_fd,0,SEEK_END);
	
	if (size == (off_t)-1) {
		perror("lseek SEEK_END");
		close(src_fd);
		return 1;
		}
	
	if (lseek(src_fd,0,SEEK_SET) == (off_t)-1) {
		perror("lseek SEEK_SET");
		close(src_fd);
		return 1;
		}
		
	int dst_fd = open(
        destination,
        O_WRONLY | O_CREAT | O_TRUNC,
        st.st_mode & 0777
    );
    
    if (dst_fd == -1) {
		perror("open destination");
		close(src_fd);
		return 1;
		}
		
	char buffer[BUFFER_SIZE];
	
	for(;;) {
		ssize_t bytes_read = read(src_fd, buffer, sizeof(buffer));
		
		if (bytes_read  == 0) {
			break;
			}
		
		if (bytes_read == -1) {
			if (errno == EINTR)
				continue;
			perror("read");
			close(src_fd);
			close(dst_fd);
			return 1;
			}
		
		
	
		ssize_t total_written = 0;
		
		while (total_written < bytes_read) {
			ssize_t bytes_written = write(
				dst_fd,
				buffer + total_written,
				(size_t)(bytes_read - total_written)
			);
			
			if (bytes_written == -1) {
				if (errno == EINTR)
					continue;
					
				perror("write");
				close(src_fd);
				close(dst_fd);
				return 1;
				}
			
			if (bytes_written == 0) {
				const char msg[] = "write returned 0\n";
				write(STDERR_FILENO, msg, sizeof(msg) - 1);
				
				close(src_fd);
				close(dst_fd);
				return 1;
				}
			
			total_written += bytes_written;
			}
		}
		
		if (close(src_fd) == -1) {
			perror("close source");
			close(dst_fd);
			return 1;
			
			}
		
		if (close(dst_fd) == -1) {
			perror("close destination");
			return 1;
			}
		
		return 0;
	}
