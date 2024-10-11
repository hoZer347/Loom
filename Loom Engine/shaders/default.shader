#version 450

#if COMPILING_VS

void main()
{
	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}

#else if COMPILING_FS

void main()
{
	// Output the color red
	gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}

#endif
