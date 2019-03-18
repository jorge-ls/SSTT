#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VERSION		24
#define BUFSIZE		8096
#define ERROR		42
#define LOG			44
#define PROHIBIDO	403
#define NOENCONTRADO	404

char * serverDir;
FILE * fich;
long int bytesFichero;
int cookieCounter;
int firstRequest = 1;

struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	 };

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	
	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}


char * buscarRecurso(DIR * dir,char * elem){
	dir = opendir(serverDir);
	//printf("El directorio del servidor es %s \n",serverDir);
	if (dir == NULL){
		printf("Error:No se puede abrir el directorio\n");
		exit(2);
	}	
	char * recurso;
	struct dirent * direntp;
	while ((direntp = readdir(dir)) != NULL){
		if (strcmp(direntp->d_name,elem) == 0){
			//printf("Se ha encontrado el recurso %s\n",elem);
			recurso = direntp->d_name;
			fich = fopen(direntp->d_name,"r");
			fseek(fich,0L,SEEK_END);
			if (ftell(fich) == 0){
				printf("El fichero esta vacio\n");
				exit(2);
			}
			bytesFichero = ftell(fich);
			closedir(dir);
			return recurso;
			//printf("El fichero %s ocupa %ld bytes \n",direntp->d_name,bytesFichero);
		}
	}
	closedir(dir);
	return NULL;
}

//Comprueba si la extension del recurso esta soportada

char * isSoportado(char * extFichero){
	int tam = sizeof(extensions);
	int tamElem = sizeof(extensions[0]);
	int longitud = tam/tamElem;
	for (int i=0;i<longitud;i++){
		if (strcmp(extensions[i].ext,extFichero) == 0){
			return extensions[i].filetype;
		}
	}
	return NULL;
}

//Obtiene la extension del recurso solicitado

char * getExtFichero(char * recurso){
	//printf("Entra getExtFichero\n");
	char * token;
	token = strtok(recurso,".");
	token = strtok(NULL,".");
	//printf("Token extension: %s\n",token);
	return token;

}


void process_web_request(int descriptorFichero)
{
	debug(LOG,"request","Ha llegado una peticion",descriptorFichero);
	//
	// Definir buffer y variables necesarias para leer las peticiones
	//
	int requestSize = 450;
	char requestBuffer[requestSize];
	fd_set readfds;
	struct timeval tv;
	int retval;
	int peticion = 1;
	char * connection;
	 
	//Se inicializa el conjunto de sockets de lectura 
	FD_ZERO(&readfds);
	//Se añade el socket al conjunto de sockets de lectura
	FD_SET(descriptorFichero,&readfds);
	
	while(peticion){
		//Se inicializan los valores del timeout
		tv.tv_sec = 10;
		tv.tv_usec = 0;
		
		retval = select(descriptorFichero+1,&readfds,NULL,NULL,&tv);
		if (retval <= 0){
			peticion = 0;
		}

		if (FD_ISSET(descriptorFichero,&readfds)){
			printf("Hay nuevos datos disponibles en el socket %d\n",descriptorFichero);
			//
			// Leer la petición HTTP
			//
			int bytesleidos = read(descriptorFichero,requestBuffer,requestSize);
			printf("Peticion http:\n\n");
			for (int i=0;i<bytesleidos;i++){
				printf("%c",requestBuffer[i]);
			}
	
			for (int i=0;i<bytesleidos;i++){
				if (requestBuffer[i] == '\r' || requestBuffer[i] == '\n'){
					requestBuffer[i] = '*';
				}
			} 
	//
	// Comprobación de errores de lectura
	//
	regex_t regex; //Estructura regex
	int reti;
	char msgbuf[100];
	char * linea;
	char * lineaGet;
	char * lineaConection;
	char * token;
	char * auxToken;
	char * directorio;
	const char delim[] = "**"; 
	char * recurso = NULL;
	DIR * dir;
	int isBadRequest = 0;
	char * tipoFichero;

	//printf("Comprobacion de errores de lectura\n");
	
	linea = strtok(requestBuffer,delim);
	lineaGet = strdup(linea);
	//printf("Linea: %s\n",linea);
	
	while (linea!=NULL){
		if (strstr(linea,"Connection:") != NULL){
			lineaConection = strdup(linea);
			//printf("Linea connection: %s\n",lineaConection);
		}
		linea = strtok(NULL,delim);
		//printf("Linea: %s\n",linea);
	}

	//Lectura de la linea de solicitud
	//printf("Lectura de la linea de solicitud\n");
	
	//printf("Linea solicitud: %s\n",lineaGet);
	token = strtok(lineaGet," ");
	if (strcmp(token,"GET") == 0){
		//printf("Token: %s\n",token);
		token = strtok(NULL," ");
		directorio = token;
		//printf("El directorio es %s\n",directorio);
		token = strtok(NULL," ");
		if (strcmp(token,"HTTP/1.1") != 0){
			isBadRequest = 1;
		}	
	}
	else{
		printf("Error: El mensaje de solicitud debe ser un GET\n");
		isBadRequest = 1;
	}
	char * auxDirectorio = strdup(directorio);
	token = strtok(auxDirectorio,"/");
	while (token != NULL){
		//printf("Token recurso: %s\n",token);
		auxToken = token;
		token = strtok(NULL,"/");	

	}
	//printf("El recurso solicitado es %s\n",auxToken);

	//Se comprueba si la conexion es persistente o no
	token = strtok(lineaConection," ");
	while (token != NULL){
		if (strcmp(token,"keep-alive") == 0){
			//printf("Conexion keep-alive\n");
			connection = token;
		}
		else if (strcmp(token,"close") == 0){
			//printf("Conexion no keep-alive\n");
			connection = token;
		}
		token = strtok(NULL," ");
	}
	
	printf("La conexion es: %s\n",connection);
	
	reti = regcomp(&regex,"^\\/(([a-zA-Z_-]*\\/?)+\\.[a-z]{3,4})?$",REG_EXTENDED);
	if( reti ){ 
		fprintf(stderr, "Could not compile regex\n"); 
		exit(1); 
	}
	reti = regexec(&regex,directorio, 0, NULL, 0);  
	if( reti == 0 ){
    		puts("Match");                               	    
		printf("La ruta introducida es válida\n");
	}
	else if( reti == REG_NOMATCH ){
    		puts("No match");
		printf("Error:La ruta introducida no es válida\n"); 
		isBadRequest = 1;                              
	}
	else if (reti == REG_BADPAT){
		puts("Invalid");
		printf("Error: La expresion regular es invalida\n");

	}
	else{                                                   
    		regerror(reti, &regex, msgbuf, sizeof(msgbuf));
    		fprintf(stderr, "Regex match failed: %s\n", msgbuf);
    		exit(1);
	}
		
		
	//
	// Si la lectura tiene datos válidos terminar el buffer con un \0
	//
	//strcat(requestBuffer,"/0");

	char buffer[BUFSIZE];
	int df;
	char * extFichero;
	
	//
	//	Como se trata el caso de acceso ilegal a directorios superiores de la
	//	jerarquia de directorios
	//	del sistema
	//
	
	
	if (strstr(directorio,"..") != NULL){
		printf("Error: No se tienen permisos para acceder al recurso\n");
		debug(PROHIBIDO,"403 Forbidden","No se tienen permisos para acceder al recurso solicitado",descriptorFichero);
		recurso = buscarRecurso(dir,"forbidden.html");
		tipoFichero = "text/html";
	}

	else if (cookieCounter == 10){
		printf("Se ha realizado el maximo numero de accesos permitidos\n");
		recurso = buscarRecurso(dir,"forbidden.html");
		tipoFichero = "text/html";
	}
	else if (strcmp(directorio,"/") == 0){
		//printf("Entra a buscar fichero index.html\n");
		recurso = buscarRecurso(dir,"index.html");
		tipoFichero = "text/html";
		
		//printf("El recurso encontrado es: %s\n",recurso);
	}
	else if (isBadRequest){
		//printf("Entra badRequest\n");
		recurso = buscarRecurso(dir,"badRequest.html");
		tipoFichero = "text/html";
	}
	else {
		//printf("Entra a buscar fichero\n");
		//Caso para tratar la solicitud del resto de ficheros
		char * recursoSolicitado = strdup(auxToken);
		extFichero = getExtFichero(auxToken);
		//printf("La extension del fichero es: %s\n",extFichero);
		tipoFichero = isSoportado(extFichero);
		if (tipoFichero == NULL){
			printf("La extension del fichero solicitado no esta soportada\n");
		}
		recurso = buscarRecurso(dir,recursoSolicitado);
		
	}
	//
	//	Como se trata el caso excepcional de la URL que no apunta a ningún fichero
	//	html
	//

	if (recurso == NULL){
		printf("Entra notFound\n");
		printf("No se ha encontrado el recurso solicitado\n");
		//debug(NOENCONTRADO,"404 Not Found","No se ha encontrado el recurso solicitado",descriptorFichero);
		recurso = buscarRecurso(dir,"notFound.html");
		tipoFichero = "text/html";
	}
		
		fseek(fich,0L,SEEK_SET);
		int pos = ftell(fich);
		time_t rawtime;
		struct tm * timeInfo;
		rawtime = time(NULL);
		timeInfo = localtime(&rawtime);
		char cabecera[100];
		char * server = "UbuntuServer/16.04"; 
		char fechayHora[50];
		char caducidad[50];
		strftime(fechayHora,50,"%c",timeInfo);
		//
		//	En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
		//	correspondiente, y el envio del fichero se hace en blockes de un máximo de  8kB
		//
		
		if (strcmp(recurso,"notFound.html") != 0 && strcmp(recurso,"forbidden.html") != 0 && strcmp(recurso,"badRequest.html") != 0){
			printf("El valor de la cookie es: %d\n",cookieCounter);
			if (firstRequest){
				printf("Primera peticion\n");
				rawtime = time(NULL);
				timeInfo = localtime(&rawtime);
				timeInfo->tm_min +=2;
				strftime(caducidad,50,"%c",timeInfo);
				cookieCounter = 1;
				firstRequest = 0;
			}
			
			//Se crean las cabeceras de la respuesta
			sprintf(cabecera,"HTTP/1.1 200 OK\r\nDate: %s\r\nServer: %s\r\nContent-Length: %ld\r\nConnection: %s\r\nContent-Type: %s\r\nSet-Cookie: counter= %d; Expires= %s\r\n\r\n",fechayHora,server,bytesFichero,connection,tipoFichero,cookieCounter,caducidad);
			if (cookieCounter < 10){
				cookieCounter++;
			}
		
		}
		else if (strcmp(recurso,"notFound.html") == 0){
			sprintf(cabecera,"HTTP/1.1 404 Not Found\r\nDate: %s\r\nServer: %s\r\nContent-Length: %ld\r\nConnection: %s\r\nContent-Type: %s\r\n\r\n",fechayHora,server,bytesFichero,connection,tipoFichero);
		}
		else if (strcmp(recurso,"forbidden.html") == 0){
			sprintf(cabecera,"HTTP/1.1 403 Forbidden\r\nDate: %s\r\nServer: %s\r\nContent-Length: %ld\r\nConnection: %s\r\nContent-Type: %s\r\n\r\n",fechayHora,server,bytesFichero,connection,tipoFichero);
		}
		else if (strcmp(recurso,"badRequest.html") == 0){
			sprintf(cabecera,"HTTP/1.1 400 Bad Request\r\nDate: %s\r\nServer: %s\r\nContent-Length: %ld\r\nConnection: %s\r\nContent-Type: %s\r\n\r\n",fechayHora,server,bytesFichero,connection,tipoFichero);
		}
		int contador = 0;
		printf("Respuesta http:\n\n");
		for (int i=0;cabecera[i]!='\0';i++){
			printf("%c",cabecera[i]);
			contador++;
		}
		//printf("El tamaño de la cabecera es: %d\n",contador);
		int bytesEscritos = write(descriptorFichero,cabecera,contador);
		//printf("El numero de bytes escritos es: %d\n",bytesEscritos);
				
		while (!feof(fich)){
			//printf("Entra bucle fichero\n");
			bytesleidos = fread(buffer,1,BUFSIZE,fich);
			pos = ftell(fich);
			//printf("La posicion actual en el fichero es: %d\n",pos);
			//printf("Bytes leidos: %d\n",bytesleidos);
			bytesEscritos = write(descriptorFichero,buffer,bytesleidos);
			//printf("El numero de bytes escritos es: %d\n",bytesEscritos);
		}
	//}
		if (strcmp(connection,"close") == 0){
			peticion = 0;
		}
	
	
	
	//
	// Se eliminan los caracteres de retorno de carro y nueva linea
	//
	
	
	//
	//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
	//	(Se soporta solo GET)
	//
		
	
	//
	//	Evaluar el tipo de fichero que se está solicitando, y actuar en
	//	consecuencia devolviendolo si se soporta u devolviendo el error correspondiente en otro caso
	//
	
	
		
		}						
				
	}
	if (strcmp(connection,"keep-alive") == 0){
		printf("Timeout expirado\n");
		printf("Timeout: Seg: %ld MiliSeg: %ld\n",tv.tv_sec,tv.tv_usec);
	}
	printf("Se ha cerrado la conexion\n");
	close(descriptorFichero);
	//exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros
	
	
	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verficiar que los argumentos que se pasan al iniciar el programa son los esperados
	//
	if (argc!=3){
		printf("ERROR:El programa debe recibir dos argumentos \n");
		exit(1);
	}
	//
	//  Verficiar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//

	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	serverDir = argv[2];
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues
	
	debug(LOG,"web server starting...", argv[1] ,getpid());
	
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);
	
	port = atoi(argv[1]);
	
	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);
	
	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/
	
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);
	
	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);
	
	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {			
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { // Proceso padre
				(void)close(socketfd);
				
			}
		}
	}
}
