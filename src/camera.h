#ifndef GETTING_STARTED_CAMERA_H
#define GETTING_STARTED_CAMERA_H

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <vector>

//Defines several possible options for camera movement. Used as abstraction to stay away from window system specific input methods
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

//Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 0.0025f;
const float SENSITIVITY = 0.05f;
const float ZOOM = 45.0f;

class Camera{
public:
    //camera attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    //euler angles
    float Yaw;
    float Pitch;
    //camera options
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    //constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f ,0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM){
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVector();
    }
    // constructor with scalar values
    Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = glm::vec3(posX, posY, posZ);
        WorldUp = glm::vec3(upX, upY, upZ);
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVector();
    }

    //returns the view matrix calculated using Euler angles and the LookAt Matrix
    glm::mat4 GetViewMatrix(){
        return glm::lookAt(Position, Position + Front, Up);
    }

    void ProcessKeyboard(Camera_Movement direction, float deltaTime){
        float velocity = MovementSpeed * deltaTime;
        if(direction == FORWARD)
            Position += velocity * Front;
        if(direction == BACKWARD)
            Position -= velocity * Front;
        if(direction == LEFT)
            Position -= velocity * Right;
        if(direction == RIGHT)
            Position += velocity * Right;
        if(direction == UP)
            Position += velocity * WorldUp;
        if(direction == DOWN)
            Position -= velocity * WorldUp;
    }

    void ProcessMouseMovement(float offsetX, float offsetY, GLboolean constrainPitch = true){
        offsetX *= MouseSensitivity;
        offsetY *= MouseSensitivity;

        Yaw += offsetX;
        Pitch += offsetY;

        if(constrainPitch){
            if(Pitch > 89.0f)
                Pitch = 89.0f;
            if(Pitch < -89.0f)
                Pitch = -89.0f;
        }

        updateCameraVector();
    }

    void ProcessMouseScroll(float offsetY){
        Zoom -= (float) offsetY;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 45.0f)
            Zoom = 45.0f;
    }

private:
    //calculate the front vector from the camera`s (updated) euler angles
    void updateCameraVector(){
        glm::vec3 front = glm::vec3 (1.0f);
        front.x = cos(glm::radians(Pitch)) * cos(glm::radians(Yaw));
        front.z = cos(glm::radians(Pitch)) * sin(glm::radians(Yaw));
        front.y = sin(glm::radians(Pitch));
        Front = glm::normalize(front);
        //also re-calculate the Up and Right vector
        //normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
};

#endif //GETTING_STARTED_CAMERA_H