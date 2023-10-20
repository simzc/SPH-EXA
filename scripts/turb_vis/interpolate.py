import numpy as np
# from PIL import Image
import sys
import h5py

container = np.fromfile("/Users/zhu0002-adm/nparray_03", dtype="f4")
container = container.reshape(1500, 1500, 1500)
original_dataset = h5py.File("/Users/zhu0002-adm/slice00003.h5", 'r')
group = original_dataset["Step#0"]
num_total_particles = len(original_dataset["Step#0"]["x"])

grid = np.linspace(-0.5, 0.5, 1500)
alpha = 1.0/(1500-1)
domain_min = -0.5
domain_max = 0.5

def get_weights(x, y, z):
    w000 = (1-x)*(1-y)*(1-z)
    w100 = x*(1-y)*(1-z)
    w010 = (1-x)*y*(1-z)
    w001 = (1-x)*(1-y)*z
    w101 = x*(1-y)*z
    w011 = (1-x)*y*z
    w110 = x*y*(1-z)
    w111 = x*y*z

    resarr = np.array([w000, w001, w010, w011, w100, w101, w110, w111])
    if len(resarr[resarr < 0]) > 0:
        print("Not possible!")
    if len(resarr[resarr > 1.0]) > 0:
        print("Not possible either!")    
    res = np.array([[[w000, w001],[w010, w011]], [[w100, w101],[w110, w111]]])
    return res

def get_grids(x, y, z):
    ind_x = np.floor((x - domain_min) / alpha)
    ind_y = np.floor((y - domain_min) / alpha)
    ind_z = np.floor((z - domain_min) / alpha)
    return [ind_x, ind_y, ind_z]

# 
def normalize(x, y, z, coords):
    grid_x = coords[0]*alpha + domain_min
    grid_y = coords[1]*alpha + domain_min
    grid_z = coords[2]*alpha + domain_min
    return [x-grid_x, y-grid_y, z-grid_z]

def assign(container, base_grid_coords, weight_arr, value):
    grid_x = int(base_grid_coords[0])
    grid_y = int(base_grid_coords[1])
    grid_z = int(base_grid_coords[2])

    container[grid_x:grid_x+2, grid_y:grid_y+2, grid_z:grid_z+2] += weight_arr*value
    return

if __name__ == "__main__":
    # Would be faster if it's read in chunks
    # container = container.reshape()
    for i in range(num_total_particles):
        x = group['x'][i]
        y = group['y'][i]
        z = group['z'][i]
        vx = group['vx'][i]
        coords = get_grids(x, y, z)

        normalized_x, normalized_y, normalized_z = normalize(x, y, z, coords)
        weights = get_weights(normalized_x, normalized_y, normalized_z)
        assign(container, coords, weights, vx)

sys.exit(0)





# print(container[(59, 302, 671)])




# x: 302, y: 671, z: 59
# x: 308, y: 671, z: 59
# 59*(3000/1500)*(3000*3000) + 671*(3000/1500)*3000 + 302*(3000/1500)
# 1066026604 - int(1066026604/(27000000000/88))*int(27000000000/88) = 1066026604.0 <- index of particle
