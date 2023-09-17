import os
import imageio
images = []

filenames = os.listdir('D:/OpenGL/raytracing-demo/Images')

for i in range(0, 151):
    images.append(imageio.imread('D:/OpenGL/raytracing-demo/Images/image_' + str(i) + '.png'))

imageio.mimsave('D:/OpenGL/raytracing-demo/demo.gif', images)
